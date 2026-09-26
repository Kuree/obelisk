//===- SimulationNBALowering.cpp - Native NBA rewrite patterns ----------===//

#include "SimulationNBALowering.h"
#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Conversion/SimulationRuntime.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHandle.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Transforms/DialectConversion.h"

#include "llvm/ADT/STLExtras.h"

#include <functional>

using namespace mlir;

namespace obelisk::detail {
namespace {

class InertialDriverConversion final
    : public OpConversionPattern<sim::SimDriverDriveInertialOp> {
public:
  InertialDriverConversion(const TypeConverter &converter, MLIRContext *context,
                           uint64_t stateBitCount)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount) {}

  LogicalResult
  matchAndRewrite(sim::SimDriverDriveInertialOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getDriver().size() != 1 || adaptor.getValue().empty() ||
        adaptor.getRiseDelay().size() != 1 ||
        adaptor.getFallDelay().size() != 1 ||
        adaptor.getTurnoffDelay().size() != 1)
      return failure();
    std::optional<unsigned> width = nativeStateWidth(op.getValue().getType());
    if (!width)
      return failure();

    Location location = op.getLoc();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value runtimeContext =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    auto savePlane = [&](Value value) {
      Value address = entryAlloca(rewriter, location, value.getType(), 1, 1);
      LLVM::StoreOp::create(rewriter, location, value, address, 1);
      return address;
    };
    Value value = savePlane(adaptor.getValue().front());
    Value unknown = LLVM::ZeroOp::create(rewriter, location, pointer);
    Value unknownPlane = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (adaptor.getValue().size() == 2) {
      unknown = savePlane(adaptor.getValue()[1]);
      unknownPlane = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                               "__obelisk_state_unknown");
    }
    uint32_t flags = 0;
    if (op.getVectorDelay())
      flags |= OBELISK_RT_INERTIAL_DRIVER_VECTOR_DELAY;
    if (op.getDeferResolution())
      flags |= OBELISK_RT_INERTIAL_DRIVER_DEFER_RESOLUTION;
    if (op->hasAttr("obelisk_sim.user_net_raw_drive"))
      flags |= OBELISK_RT_INERTIAL_DRIVER_PUBLISH_RAW;
    if (op.getValue().getType().isF32())
      flags |= OBELISK_RT_INERTIAL_DRIVER_REAL32;
    if (op.getValue().getType().isF64())
      flags |= OBELISK_RT_INERTIAL_DRIVER_REAL64;
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_scheduler_inertial_driver"),
            ValueRange{
                runtimeContext,
                LLVM::AddressOfOp::create(rewriter, location, pointer,
                                          "__obelisk_state_value"),
                unknownPlane,
                llvmConstant(rewriter, location, i64, stateBitCount),
                adaptor.getDriver().front(),
                llvmConstant(rewriter, location, i64, *width),
                llvmConstant(rewriter, location, i64, op.getCodeUnitId()),
                llvmConstant(rewriter, location, i32, op.getComponent()),
                llvmConstant(rewriter, location, i32, flags),
                adaptor.getRiseDelay().front(), adaptor.getFallDelay().front(),
                adaptor.getTurnoffDelay().front(), value, unknown})
            .getResult();
    LLVM::CallOp::create(rewriter, location, TypeRange{},
                         SymbolRefAttr::get(rewriter.getContext(),
                                            "obelisk_rt_v1_scheduler_fail"),
                         ValueRange{runtimeContext, status});
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount = 0;
};

class InertialPathDriverConversion final
    : public OpConversionPattern<sim::SimDriverDriveInertialPathOp> {
public:
  InertialPathDriverConversion(const TypeConverter &converter,
                               MLIRContext *context, uint64_t stateBitCount)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount) {}

  LogicalResult
  matchAndRewrite(sim::SimDriverDriveInertialPathOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getDriver().size() != 1 || adaptor.getValue().empty() ||
        adaptor.getActiveMask().size() != 1 ||
        adaptor.getRiseMask().size() != 1 ||
        adaptor.getFallMask().size() != 1 ||
        adaptor.getTurnoffMask().size() != 1 ||
        adaptor.getPulseTransitionMasks().size() > 1 ||
        adaptor.getRiseDelay().size() != 1 ||
        adaptor.getFallDelay().size() != 1 ||
        adaptor.getTurnoffDelay().size() != 1)
      return failure();
    std::optional<unsigned> width = nativeStateWidth(op.getValue().getType());
    if (!width)
      return failure();

    Location location = op.getLoc();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value runtimeContext =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    auto savePlane = [&](Value value) {
      Value address = entryAlloca(rewriter, location, value.getType(), 1, 1);
      LLVM::StoreOp::create(rewriter, location, value, address, 1);
      return address;
    };
    Value value = savePlane(adaptor.getValue().front());
    Value unknown = LLVM::ZeroOp::create(rewriter, location, pointer);
    Value unknownPlane = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (adaptor.getValue().size() == 2) {
      unknown = savePlane(adaptor.getValue()[1]);
      unknownPlane = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                               "__obelisk_state_unknown");
    }
    bool pulseControlled = !adaptor.getPulseTransitionMasks().empty();
    uint32_t flags = 0;
    if (op.getDeferResolution())
      flags |= OBELISK_RT_INERTIAL_DRIVER_DEFER_RESOLUTION;
    if (op->hasAttr("obelisk_sim.user_net_raw_drive"))
      flags |= OBELISK_RT_INERTIAL_DRIVER_PUBLISH_RAW;
    if (op.getPulseOnDetect())
      flags |= OBELISK_RT_INERTIAL_PATH_ON_DETECT;
    if (op.getPulseShowCancelled())
      flags |= OBELISK_RT_INERTIAL_PATH_SHOW_CANCELLED;
    if (op.getPulseTransitionMasks())
      flags |= OBELISK_RT_INERTIAL_PATH_EXACT_TRANSITIONS;
    SmallVector<Value, 23> arguments{
        runtimeContext,
        LLVM::AddressOfOp::create(rewriter, location, pointer,
                                  "__obelisk_state_value"),
        unknownPlane,
        llvmConstant(rewriter, location, i64, stateBitCount),
        adaptor.getDriver().front(),
        llvmConstant(rewriter, location, i64, *width),
        llvmConstant(rewriter, location, i64, op.getCodeUnitId()),
        llvmConstant(rewriter, location, i32, op.getComponent()),
        llvmConstant(rewriter, location, i32, op.getGroup()),
        llvmConstant(rewriter, location, i32, op.getGroupCount()),
        llvmConstant(rewriter, location, i32, flags),
        adaptor.getRiseDelay().front(),
        adaptor.getFallDelay().front(),
        adaptor.getTurnoffDelay().front()};
    if (pulseControlled) {
      arguments.push_back(llvmConstant(
          rewriter, location, i64, static_cast<uint64_t>(op.getPulseReject())));
      arguments.push_back(llvmConstant(
          rewriter, location, i64, static_cast<uint64_t>(op.getPulseError())));
    }
    arguments.push_back(value);
    arguments.push_back(unknown);
    arguments.push_back(savePlane(adaptor.getActiveMask().front()));
    arguments.push_back(savePlane(adaptor.getRiseMask().front()));
    arguments.push_back(savePlane(adaptor.getFallMask().front()));
    arguments.push_back(savePlane(adaptor.getTurnoffMask().front()));
    if (pulseControlled)
      arguments.push_back(savePlane(adaptor.getPulseTransitionMasks().front()));
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(
                rewriter.getContext(),
                pulseControlled
                    ? "obelisk_rt_v1_scheduler_inertial_path_driver_pulse"
                    : "obelisk_rt_v1_scheduler_inertial_path_driver"),
            arguments)
            .getResult();
    LLVM::CallOp::create(rewriter, location, TypeRange{},
                         SymbolRefAttr::get(rewriter.getContext(),
                                            "obelisk_rt_v1_scheduler_fail"),
                         ValueRange{runtimeContext, status});
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount = 0;
};

class InertialPathStorageConversion final
    : public OpConversionPattern<sim::SimRefStoreInertialPathOp> {
public:
  InertialPathStorageConversion(const TypeConverter &converter,
                                MLIRContext *context, uint64_t stateBitCount)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount) {}

  LogicalResult
  matchAndRewrite(sim::SimRefStoreInertialPathOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getReference().size() != 1 || adaptor.getValue().empty() ||
        adaptor.getWriteMask().size() != 1 ||
        adaptor.getActiveMask().size() != 1 ||
        adaptor.getRiseMask().size() != 1 ||
        adaptor.getFallMask().size() != 1 ||
        adaptor.getTurnoffMask().size() != 1 ||
        adaptor.getRiseDelay().size() != 1 ||
        adaptor.getFallDelay().size() != 1 ||
        adaptor.getTurnoffDelay().size() != 1)
      return failure();
    std::optional<unsigned> width = nativeStateWidth(op.getValue().getType());
    if (!width)
      return failure();

    Location location = op.getLoc();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value runtimeContext =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    auto savePlane = [&](Value value) {
      Value address = entryAlloca(rewriter, location, value.getType(), 1, 1);
      LLVM::StoreOp::create(rewriter, location, value, address, 1);
      return address;
    };
    Value value = savePlane(adaptor.getValue().front());
    Value unknown = LLVM::ZeroOp::create(rewriter, location, pointer);
    Value unknownPlane = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (adaptor.getValue().size() == 2) {
      unknown = savePlane(adaptor.getValue()[1]);
      unknownPlane = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                               "__obelisk_state_unknown");
    }
    bool pulseControlled = !adaptor.getPulseTransitionMasks().empty();
    SmallVector<Value, 24> arguments{
        runtimeContext,
        LLVM::AddressOfOp::create(rewriter, location, pointer,
                                  "__obelisk_state_value"),
        unknownPlane,
        llvmConstant(rewriter, location, i64, stateBitCount),
        adaptor.getReference().front(),
        llvmConstant(rewriter, location, i64, *width),
        llvmConstant(rewriter, location, i64, op.getSiteId()),
        llvmConstant(rewriter, location, i32, op.getComponent()),
        llvmConstant(rewriter, location, i32, op.getGroup()),
        llvmConstant(rewriter, location, i32, op.getGroupCount()),
        llvmConstant(rewriter, location, i32, op.getNonblocking())};
    if (pulseControlled) {
      arguments.push_back(llvmConstant(
          rewriter, location, i32,
          (op.getPulseOnDetect() ? OBELISK_RT_INERTIAL_PATH_ON_DETECT : 0) |
              (op.getPulseShowCancelled()
                   ? OBELISK_RT_INERTIAL_PATH_SHOW_CANCELLED
                   : 0) |
              (op.getPulseTransitionMasks()
                   ? OBELISK_RT_INERTIAL_PATH_EXACT_TRANSITIONS
                   : 0)));
    }
    arguments.push_back(adaptor.getRiseDelay().front());
    arguments.push_back(adaptor.getFallDelay().front());
    arguments.push_back(adaptor.getTurnoffDelay().front());
    if (pulseControlled) {
      arguments.push_back(llvmConstant(
          rewriter, location, i64, static_cast<uint64_t>(op.getPulseReject())));
      arguments.push_back(llvmConstant(
          rewriter, location, i64, static_cast<uint64_t>(op.getPulseError())));
    }
    arguments.push_back(value);
    arguments.push_back(unknown);
    arguments.push_back(savePlane(adaptor.getWriteMask().front()));
    arguments.push_back(savePlane(adaptor.getActiveMask().front()));
    arguments.push_back(savePlane(adaptor.getRiseMask().front()));
    arguments.push_back(savePlane(adaptor.getFallMask().front()));
    arguments.push_back(savePlane(adaptor.getTurnoffMask().front()));
    if (pulseControlled)
      arguments.push_back(savePlane(adaptor.getPulseTransitionMasks().front()));
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(
                rewriter.getContext(),
                pulseControlled
                    ? "obelisk_rt_v1_scheduler_inertial_path_storage_pulse"
                    : "obelisk_rt_v1_scheduler_inertial_path_storage"),
            arguments)
            .getResult();
    LLVM::CallOp::create(rewriter, location, TypeRange{},
                         SymbolRefAttr::get(rewriter.getContext(),
                                            "obelisk_rt_v1_scheduler_fail"),
                         ValueRange{runtimeContext, status});
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount = 0;
};

class InertialStrengthPairConversion final
    : public OpConversionPattern<sim::SimDriverDriveInertialStrengthPairOp> {
public:
  InertialStrengthPairConversion(const TypeConverter &converter,
                                 MLIRContext *context, uint64_t stateBitCount)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount) {}

  LogicalResult
  matchAndRewrite(sim::SimDriverDriveInertialStrengthPairOp op,
                  OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getLowDriver().size() != 1 ||
        adaptor.getHighDriver().size() != 1 ||
        adaptor.getLowValue().size() != 2 ||
        adaptor.getHighValue().size() != 2 ||
        adaptor.getTransitionValue().size() != 2 ||
        adaptor.getRiseDelay().size() != 1 ||
        adaptor.getFallDelay().size() != 1 ||
        adaptor.getTurnoffDelay().size() != 1)
      return failure();
    std::optional<unsigned> width =
        nativeStateWidth(op.getLowValue().getType());
    if (!width)
      return failure();

    Location location = op.getLoc();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value runtimeContext =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    auto savePlane = [&](Value value) {
      Value address = entryAlloca(rewriter, location, value.getType(), 1, 1);
      LLVM::StoreOp::create(rewriter, location, value, address, 1);
      return address;
    };
    Value lowValue = savePlane(adaptor.getLowValue()[0]);
    Value lowUnknown = savePlane(adaptor.getLowValue()[1]);
    Value highValue = savePlane(adaptor.getHighValue()[0]);
    Value highUnknown = savePlane(adaptor.getHighValue()[1]);
    Value transitionValue = savePlane(adaptor.getTransitionValue()[0]);
    Value transitionUnknown = savePlane(adaptor.getTransitionValue()[1]);
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(
                rewriter.getContext(),
                "obelisk_rt_v1_scheduler_inertial_driver_strength_pair"),
            ValueRange{
                runtimeContext,
                LLVM::AddressOfOp::create(rewriter, location, pointer,
                                          "__obelisk_state_value"),
                LLVM::AddressOfOp::create(rewriter, location, pointer,
                                          "__obelisk_state_unknown"),
                llvmConstant(rewriter, location, i64, stateBitCount),
                adaptor.getLowDriver().front(), adaptor.getHighDriver().front(),
                llvmConstant(rewriter, location, i64, *width),
                llvmConstant(rewriter, location, i64, op.getCodeUnitId()),
                llvmConstant(rewriter, location, i32, op.getComponent()),
                adaptor.getRiseDelay().front(), adaptor.getFallDelay().front(),
                adaptor.getTurnoffDelay().front(), lowValue, lowUnknown,
                highValue, highUnknown, transitionValue, transitionUnknown})
            .getResult();
    LLVM::CallOp::create(rewriter, location, TypeRange{},
                         SymbolRefAttr::get(rewriter.getContext(),
                                            "obelisk_rt_v1_scheduler_fail"),
                         ValueRange{runtimeContext, status});
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount = 0;
};

class InertialPathStrengthPairConversion final
    : public OpConversionPattern<
          sim::SimDriverDriveInertialPathStrengthPairOp> {
public:
  InertialPathStrengthPairConversion(const TypeConverter &converter,
                                     MLIRContext *context,
                                     uint64_t stateBitCount)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount) {}

  LogicalResult
  matchAndRewrite(sim::SimDriverDriveInertialPathStrengthPairOp op,
                  OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getLowDriver().size() != 1 ||
        adaptor.getHighDriver().size() != 1 ||
        adaptor.getLowValue().size() != 2 ||
        adaptor.getHighValue().size() != 2 ||
        adaptor.getTransitionValue().size() != 2 ||
        adaptor.getActiveMask().size() != 1 ||
        adaptor.getRiseMask().size() != 1 ||
        adaptor.getFallMask().size() != 1 ||
        adaptor.getTurnoffMask().size() != 1 ||
        adaptor.getRiseDelay().size() != 1 ||
        adaptor.getFallDelay().size() != 1 ||
        adaptor.getTurnoffDelay().size() != 1)
      return failure();
    std::optional<unsigned> width =
        nativeStateWidth(op.getLowValue().getType());
    if (!width)
      return failure();
    Location location = op.getLoc();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value runtimeContext =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    auto savePlane = [&](Value value) {
      Value address = entryAlloca(rewriter, location, value.getType(), 1, 1);
      LLVM::StoreOp::create(rewriter, location, value, address, 1);
      return address;
    };
    SmallVector<Value> arguments{
        runtimeContext,
        LLVM::AddressOfOp::create(rewriter, location, pointer,
                                  "__obelisk_state_value"),
        LLVM::AddressOfOp::create(rewriter, location, pointer,
                                  "__obelisk_state_unknown"),
        llvmConstant(rewriter, location, i64, stateBitCount),
        adaptor.getLowDriver().front(),
        adaptor.getHighDriver().front(),
        llvmConstant(rewriter, location, i64, *width),
        llvmConstant(rewriter, location, i64, op.getCodeUnitId()),
        llvmConstant(rewriter, location, i32, op.getComponent()),
        llvmConstant(rewriter, location, i32, op.getGroup()),
        llvmConstant(rewriter, location, i32, op.getGroupCount())};
    bool pulseControlled = !adaptor.getPulseTransitionMasks().empty();
    if (pulseControlled) {
      uint32_t flags =
          OBELISK_RT_INERTIAL_PATH_EXACT_TRANSITIONS |
          (op.getPulseOnDetect() ? OBELISK_RT_INERTIAL_PATH_ON_DETECT : 0) |
          (op.getPulseShowCancelled() ? OBELISK_RT_INERTIAL_PATH_SHOW_CANCELLED
                                      : 0);
      arguments.push_back(llvmConstant(rewriter, location, i32, flags));
    }
    llvm::append_range(arguments,
                       ValueRange{adaptor.getRiseDelay().front(),
                                  adaptor.getFallDelay().front(),
                                  adaptor.getTurnoffDelay().front()});
    if (pulseControlled) {
      arguments.push_back(
          llvmConstant(rewriter, location, i64, op.getPulseReject()));
      arguments.push_back(
          llvmConstant(rewriter, location, i64, op.getPulseError()));
    }
    llvm::append_range(arguments,
                       ValueRange{savePlane(adaptor.getLowValue()[0]),
                                  savePlane(adaptor.getLowValue()[1]),
                                  savePlane(adaptor.getHighValue()[0]),
                                  savePlane(adaptor.getHighValue()[1]),
                                  savePlane(adaptor.getTransitionValue()[0]),
                                  savePlane(adaptor.getTransitionValue()[1]),
                                  savePlane(adaptor.getActiveMask().front()),
                                  savePlane(adaptor.getRiseMask().front()),
                                  savePlane(adaptor.getFallMask().front()),
                                  savePlane(adaptor.getTurnoffMask().front())});
    if (pulseControlled)
      arguments.push_back(savePlane(adaptor.getPulseTransitionMasks().front()));
    StringRef function =
        pulseControlled
            ? "obelisk_rt_v1_scheduler_inertial_path_strength_pair_pulse"
            : "obelisk_rt_v1_scheduler_inertial_path_strength_pair";
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(), function), arguments)
            .getResult();
    LLVM::CallOp::create(rewriter, location, TypeRange{},
                         SymbolRefAttr::get(rewriter.getContext(),
                                            "obelisk_rt_v1_scheduler_fail"),
                         ValueRange{runtimeContext, status});
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount = 0;
};

/// Preserve the declaration boundary of a dynamic packed reference through
/// native lowering. A stable handle records only a root-relative offset; the
/// input reference type supplies the narrower view width needed to clip a
/// partial NBA without spilling into an adjacent aggregate element.
class PackedSliceNBAConversion final
    : public OpConversionPattern<sim::SimNBAEnqueueOp> {
public:
  PackedSliceNBAConversion(const TypeConverter &converter, MLIRContext *context,
                           uint64_t stateBitCount,
                           const NativeStaticNBAPlan *staticPlan,
                           const NativeStateLayout *stateLayout,
                           bool staticSitesEnabled, bool guardedClaims)
      : OpConversionPattern(converter, context, PatternBenefit(2)),
        stateBitCount(stateBitCount), staticPlan(staticPlan),
        stateLayout(stateLayout), staticSitesEnabled(staticSitesEnabled),
        guardedClaims(guardedClaims) {}

  LogicalResult
  matchAndRewrite(sim::SimNBAEnqueueOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto view = op.getDestination().getDefiningOp<sim::SimRefDynExtractOp>();
    if (!view || adaptor.getValue().empty() ||
        adaptor.getDestination().size() != 1)
      return failure();
    std::optional<unsigned> baseWidth =
        sim::getPackedWidth(view.getInput().getType().getElementType());
    std::optional<unsigned> sourceWidth =
        nativeStateWidth(op.getValue().getType());
    if (!baseWidth || !sourceWidth || *baseWidth == 0 || *sourceWidth == 0)
      return failure();

    Value base = rewriter.getRemappedValue(view.getInput());
    SmallVector<Value> lowPlanes;
    if (!base ||
        failed(rewriter.getRemappedValues(view.getLowBit(), lowPlanes)) ||
        lowPlanes.empty() || lowPlanes.size() > 2)
      return failure();

    Location location = op.getLoc();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    SignedI64Index low =
        resizeSignedIndexToI64(rewriter, location, lowPlanes.front());
    Value valid = low.representable;
    if (lowPlanes.size() == 2) {
      Value known = arith::CmpIOp::create(
          rewriter, location, arith::CmpIPredicate::eq, lowPlanes[1],
          arith::ConstantOp::create(
              rewriter, location, lowPlanes[1].getType(),
              rewriter.getZeroAttr(lowPlanes[1].getType())));
      valid = arith::AndIOp::create(rewriter, location, valid, known);
    }
    // Dynamic-reference lowering may have consumed an unknown index plane
    // while building a guarded stable handle.  Preserve that exact validity
    // predicate, but not the selected handle result: direct staging needs the
    // former to make an X/Z write a no-op (IEEE 1800-2017 11.5.1), while
    // retaining the latter would keep native_handle_offset on the hot path.
    Value directValid = valid;
    bool exactDestinationValidity = true;
    Operation *destinationSelect =
        adaptor.getDestination().front().getDefiningOp();
    if (isa_and_nonnull<arith::SelectOp, LLVM::SelectOp>(destinationSelect)) {
      std::optional<uint64_t> trueValue =
          resolveCFGConstantInteger(destinationSelect->getOperand(1));
      std::optional<uint64_t> falseValue =
          resolveCFGConstantInteger(destinationSelect->getOperand(2));
      Value condition = destinationSelect->getOperand(0);
      if (falseValue && *falseValue == UINT64_MAX) {
        directValid =
            arith::AndIOp::create(rewriter, location, directValid, condition);
      } else if (trueValue && *trueValue == UINT64_MAX) {
        Value inverted = arith::XOrIOp::create(
            rewriter, location, condition,
            llvmConstant(rewriter, location, rewriter.getI1Type(), 1));
        directValid =
            arith::AndIOp::create(rewriter, location, directValid, inverted);
      } else {
        exactDestinationValidity = false;
      }
    }
    sim::NBASiteAttr site = op.getSiteAttr();
    bool mappedSite =
        site && staticPlan && staticPlan->siteRoots.contains(site.getId());
    uint32_t plannedRoot =
        mappedSite ? staticPlan->siteRoots.lookup(site.getId()) : UINT32_MAX;
    bool staticallyStaged =
        !op.getClockingOutputAttr() && staticSitesEnabled && staticPlan &&
        mappedSite && staticPlan->mergeSafeRoots[plannedRoot] &&
        adaptor.getDelay().empty() && !site.getTiming() &&
        site.getStorage() != sim::ComputeNBAStorageKind::DynamicFrontier;

    uint32_t rootIndex = UINT32_MAX;
    if (staticallyStaged) {
      auto staticRoot = staticPlan->siteRoots.find(site.getId());
      if (staticRoot != staticPlan->siteRoots.end())
        rootIndex = staticRoot->second;
    }
    std::function<std::optional<uint64_t>(Value)> resolveViewOffset =
        [&](Value reference) -> std::optional<uint64_t> {
      if (reference.getDefiningOp<sim::SimContextStorageOp>())
        return 0;
      // Capture specialization replaces direct descriptor arguments with a
      // context-storage op. A surviving entry argument can be a runtime view;
      // descriptor provenance alone does not prove root-relative offset zero.
      if (auto argument = dyn_cast<BlockArgument>(reference)) {
        auto owner =
            dyn_cast<sim::SimFuncOp>(argument.getOwner()->getParentOp());
        if (!owner || argument.getOwner() != &owner.getBody().front() ||
            !owner->hasAttr("obelisk.eval.raw_captures") || !stateLayout ||
            rootIndex == UINT32_MAX || !staticPlan ||
            rootIndex >= staticPlan->roots.size())
          return std::nullopt;
        auto descriptor = owner.getArgAttrOfType<IntegerAttr>(
            argument.getArgNumber(), "obelisk_sim.descriptor_id");
        auto handle = descriptor
                          ? stateLayout->storage.find(descriptor.getInt())
                          : stateLayout->storage.end();
        obelisk_rt_stable_handle_v1 decoded{};
        if (handle == stateLayout->storage.end() ||
            !obelisk_rt_stable_handle_decode(handle->second, &decoded) ||
            decoded.kind != OBELISK_RT_STABLE_HANDLE_STATIC ||
            decoded.id != staticPlan->roots[rootIndex].static_state ||
            decoded.offset < 0)
          return std::nullopt;
        return static_cast<uint64_t>(decoded.offset);
      }
      if (auto extract = reference.getDefiningOp<sim::SimRefExtractOp>()) {
        std::optional<uint64_t> parent = resolveViewOffset(extract.getInput());
        if (!parent || extract.getLowBit() > UINT64_MAX - *parent)
          return std::nullopt;
        return *parent + extract.getLowBit();
      }
      if (auto subelement =
              reference.getDefiningOp<sim::SimRefSubelementOp>()) {
        std::optional<uint64_t> parent =
            resolveViewOffset(subelement.getInput());
        if (!parent)
          return std::nullopt;
        uint64_t offset = *parent;
        Type type = subelement.getInput().getType().getElementType();
        for (int64_t rawIndex : subelement.getIndices()) {
          if (rawIndex < 0)
            return std::nullopt;
          auto child = sim::getAggregateProvenanceSubelement(
              type, static_cast<unsigned>(rawIndex));
          if (!child || child->first > UINT64_MAX - offset)
            return std::nullopt;
          offset += child->first;
          type = sim::getAggregateElementType(type,
                                              static_cast<unsigned>(rawIndex));
        }
        return offset;
      }
      return std::nullopt;
    };

    // Inspect original Simulation reference provenance only when it can enable
    // the generated static path. Generic conversion may already have rewritten
    // producers while legalizing an unplanned function, so needlessly walking
    // that IR is both wasted compile time and unsafe during dialect conversion.
    std::optional<uint64_t> viewOffset;
    if (staticallyStaged && rootIndex != UINT32_MAX)
      viewOffset = resolveViewOffset(view.getInput());
    StringRef generatedAccumulator =
        staticPlan && rootIndex < staticPlan->generatedAccumulators.size()
            ? staticPlan->generatedAccumulators[rootIndex]
            : StringRef{};
    uint64_t rootWidth = staticPlan && rootIndex < staticPlan->roots.size()
                             ? staticPlan->roots[rootIndex].bit_width
                             : 0;
    sim::SimFuncOp function = op->getParentOfType<sim::SimFuncOp>();
    uint32_t homeRegion =
        function ? getRuntimeEventRegion(function.getHomeRegion()) : UINT32_MAX;
    uint32_t commitRegion = homeRegion == OBELISK_RT_REGION_ACTIVE ||
                                    homeRegion == OBELISK_RT_REGION_REACTIVE
                                ? homeRegion + 2
                                : UINT32_MAX;
    bool scalarValue =
        llvm::all_of(
            adaptor.getValue(),
            [](Value value) { return isa<IntegerType>(value.getType()); }) ||
        (adaptor.getValue().size() == 1 &&
         isa<LLVM::LLVMPointerType>(adaptor.getValue().front().getType()));
    bool directGeneratedStage =
        staticallyStaged && rootIndex != UINT32_MAX &&
        !generatedAccumulator.empty() && rootWidth <= 64 &&
        *sourceWidth <= 64 && viewOffset && *viewOffset <= rootWidth &&
        *baseWidth <= rootWidth - *viewOffset && commitRegion != UINT32_MAX &&
        scalarValue && exactDestinationValidity;

    auto widen = [&](Value value) {
      if (isa<LLVM::LLVMPointerType>(value.getType()))
        value = LLVM::LoadOp::create(
            rewriter, location,
            IntegerType::get(rewriter.getContext(), *sourceWidth), value, 1);
      auto type = cast<IntegerType>(value.getType());
      return type.getWidth() == 64
                 ? value
                 : LLVM::ZExtOp::create(rewriter, location, i64, value)
                       .getResult();
    };
    auto emitDirectGeneratedStage = [&] {
      Value zero = llvmConstant(rewriter, location, i64, 0);
      Value sourceWidthValue =
          llvmConstant(rewriter, location, i64, *sourceWidth);
      Value baseWidthValue = llvmConstant(rewriter, location, i64, *baseWidth);
      Value lowerBound = llvmConstant(rewriter, location, i64,
                                      -static_cast<int64_t>(*sourceWidth));
      Value belowEnd =
          arith::CmpIOp::create(rewriter, location, arith::CmpIPredicate::slt,
                                low.value, baseWidthValue);
      Value aboveBegin = arith::CmpIOp::create(
          rewriter, location, arith::CmpIPredicate::sgt, low.value, lowerBound);
      Value overlaps = arith::AndIOp::create(
          rewriter, location, directValid,
          arith::AndIOp::create(rewriter, location, belowEnd, aboveBegin));
      Value lowPositive = arith::CmpIOp::create(
          rewriter, location, arith::CmpIPredicate::sgt, low.value, zero);
      Value start = arith::SelectOp::create(rewriter, location, lowPositive,
                                            low.value, zero);
      Value rawEnd = arith::AddIOp::create(rewriter, location, low.value,
                                           sourceWidthValue);
      Value endBelowBound =
          arith::CmpIOp::create(rewriter, location, arith::CmpIPredicate::slt,
                                rawEnd, baseWidthValue);
      Value end = arith::SelectOp::create(rewriter, location, endBelowBound,
                                          rawEnd, baseWidthValue);
      Value overlapWidth =
          arith::SubIOp::create(rewriter, location, end, start);
      Value sourceStart =
          arith::SubIOp::create(rewriter, location, start, low.value);
      Value destinationStart = arith::AddIOp::create(
          rewriter, location, start,
          llvmConstant(rewriter, location, i64, *viewOffset));
      Value safeWidth = arith::SelectOp::create(rewriter, location, overlaps,
                                                overlapWidth, zero);
      Value safeSource = arith::SelectOp::create(rewriter, location, overlaps,
                                                 sourceStart, zero);
      Value safeDestination = arith::SelectOp::create(
          rewriter, location, overlaps, destinationStart, zero);
      Value widthIs64 = arith::CmpIOp::create(
          rewriter, location, arith::CmpIPredicate::eq, safeWidth,
          llvmConstant(rewriter, location, i64, 64));
      Value maskShift = arith::SelectOp::create(
          rewriter, location, widthIs64,
          llvmConstant(rewriter, location, i64, 63), safeWidth);
      Value lowMask = arith::SubIOp::create(
          rewriter, location,
          arith::ShLIOp::create(rewriter, location,
                                llvmConstant(rewriter, location, i64, 1),
                                maskShift),
          llvmConstant(rewriter, location, i64, 1));
      lowMask = arith::SelectOp::create(
          rewriter, location, widthIs64,
          llvmConstant(rewriter, location, i64, UINT64_MAX), lowMask);
      Value mask =
          arith::ShLIOp::create(rewriter, location, lowMask, safeDestination);
      Value sourceValue = widen(adaptor.getValue().front());
      Value sourceUnknown = llvmConstant(rewriter, location, i64, 0);
      if (adaptor.getValue().size() == 2)
        sourceUnknown = widen(adaptor.getValue()[1]);
      auto position = [&](Value source) {
        return arith::AndIOp::create(
            rewriter, location,
            arith::ShLIOp::create(
                rewriter, location,
                arith::ShRUIOp::create(rewriter, location, source, safeSource),
                safeDestination),
            mask);
      };
      Value accumulator = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                    generatedAccumulator);
      if (rootIndex < staticPlan->trackTransients.size() &&
          staticPlan->trackTransients[rootIndex])
        emitGeneratedNBATransient(rewriter, location, accumulator, 0, mask,
                                  position(sourceValue),
                                  position(sourceUnknown));
      auto mergeField = [&](size_t fieldOffset, Value positioned) {
        Value address = byteGEP(rewriter, location, accumulator, fieldOffset);
        Value previous =
            LLVM::LoadOp::create(rewriter, location, i64, address, 8);
        Value merged = arith::OrIOp::create(
            rewriter, location,
            arith::AndIOp::create(
                rewriter, location, previous,
                arith::XOrIOp::create(
                    rewriter, location, mask,
                    llvmConstant(rewriter, location, i64, UINT64_MAX))),
            positioned);
        LLVM::StoreOp::create(rewriter, location, merged, address, 8);
      };
      mergeField(offsetof(obelisk_rt_generated_nba_accumulator_256, value),
                 position(sourceValue));
      mergeField(offsetof(obelisk_rt_generated_nba_accumulator_256, unknown),
                 position(sourceUnknown));
      Value maskAddress = byteGEP(
          rewriter, location, accumulator,
          offsetof(obelisk_rt_generated_nba_accumulator_256, write_mask));
      Value previousMask =
          LLVM::LoadOp::create(rewriter, location, i64, maskAddress, 8);
      LLVM::StoreOp::create(
          rewriter, location,
          arith::OrIOp::create(rewriter, location, previousMask, mask),
          maskAddress, 8);
      Value validAddress =
          byteGEP(rewriter, location, accumulator,
                  offsetof(obelisk_rt_generated_nba_accumulator_256, valid));
      Value previousValid =
          LLVM::LoadOp::create(rewriter, location, i32, validAddress, 4);
      LLVM::StoreOp::create(
          rewriter, location,
          arith::OrIOp::create(
              rewriter, location, previousValid,
              LLVM::ZExtOp::create(rewriter, location, i32, overlaps)),
          validAddress, 4);
      LLVM::StoreOp::create(
          rewriter, location,
          llvmConstant(rewriter, location, i32, commitRegion),
          byteGEP(
              rewriter, location, accumulator,
              offsetof(obelisk_rt_generated_nba_accumulator_256, exec_region)),
          4);
      Value dirtyBit =
          arith::SelectOp::create(rewriter, location, overlaps,
                                  llvmConstant(rewriter, location, i64,
                                               uint64_t{1} << (rootIndex % 64)),
                                  zero);
      Value dirtyBase = LLVM::AddressOfOp::create(
          rewriter, location, pointer, "__obelisk_aot_nba_dirty_roots_v1");
      Value dirtyAddress =
          byteGEP(rewriter, location, dirtyBase,
                  static_cast<uint64_t>(rootIndex / 64) * sizeof(uint64_t));
      Value previousDirty =
          LLVM::LoadOp::create(rewriter, location, i64, dirtyAddress, 8);
      LLVM::StoreOp::create(
          rewriter, location,
          arith::OrIOp::create(rewriter, location, previousDirty, dirtyBit),
          dirtyAddress, 8);
      uint32_t dirtyWord = rootIndex / 64;
      Value summaryBase = LLVM::AddressOfOp::create(
          rewriter, location, pointer, "__obelisk_aot_nba_dirty_summary_v1");
      Value summaryAddress =
          byteGEP(rewriter, location, summaryBase,
                  static_cast<uint64_t>(dirtyWord / 64) * sizeof(uint64_t));
      Value summaryBit =
          arith::SelectOp::create(rewriter, location, overlaps,
                                  llvmConstant(rewriter, location, i64,
                                               uint64_t{1} << (dirtyWord % 64)),
                                  zero);
      Value previousSummary =
          LLVM::LoadOp::create(rewriter, location, i64, summaryAddress, 8);
      LLVM::StoreOp::create(
          rewriter, location,
          arith::OrIOp::create(rewriter, location, previousSummary, summaryBit),
          summaryAddress, 8);
    };

    Block *guardContinuation = nullptr;
    if (directGeneratedStage) {
      bool assumeClean = op->hasAttr(assumeCleanSpecializationAttr);
      bool useGuardedClaim = !assumeClean && guardedClaims;
      if (!useGuardedClaim) {
        emitDirectGeneratedStage();
        rewriter.eraseOp(op);
        return success();
      }
      Value useDirect =
          staticNBASpecializationGuard(rewriter, location, rootIndex);
      Block *head = rewriter.getInsertionBlock();
      guardContinuation =
          rewriter.splitBlock(head, rewriter.getInsertionPoint());
      Region *region = head->getParent();
      Block *direct =
          rewriter.createBlock(region, guardContinuation->getIterator());
      Block *fallback =
          rewriter.createBlock(region, guardContinuation->getIterator());
      recordStaticSpecializationCFGBlocks(rewriter, head, 3);
      rewriter.setInsertionPointToEnd(head);
      markLikelyTrue(cf::CondBranchOp::create(rewriter, location, useDirect,
                                              direct, ValueRange{}, fallback,
                                              ValueRange{}));
      rewriter.setInsertionPointToEnd(direct);
      emitDirectGeneratedStage();
      cf::BranchOp::create(rewriter, location, guardContinuation);
      rewriter.setInsertionPointToEnd(fallback);
    }

    // Generic fallback consumes the converted handle contract.  Build this
    // only after the direct path has returned: otherwise its handle-offset
    // call remains artificially live in a runtime-free eval body even though
    // direct staging has already reconstructed index knownness and bounds.
    valid = arith::AndIOp::create(
        rewriter, location, valid,
        arith::CmpIOp::create(
            rewriter, location, arith::CmpIPredicate::ne,
            adaptor.getDestination().front(),
            llvmConstant(rewriter, location, i64, UINT64_MAX)));
    Value valid32 = LLVM::ZExtOp::create(rewriter, location, i32, valid);
    auto savePlane = [&](Value value) {
      Value address = entryAlloca(rewriter, location, value.getType(), 1, 1);
      LLVM::StoreOp::create(rewriter, location, value, address, 1);
      return address;
    };
    Value value = savePlane(adaptor.getValue().front());
    Value unknown = LLVM::ZeroOp::create(rewriter, location, pointer);
    Value unknownPlane = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (adaptor.getValue().size() == 2) {
      unknown = savePlane(adaptor.getValue()[1]);
      unknownPlane = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                               "__obelisk_state_unknown");
    }
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value runtimeContext =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    Value delay = adaptor.getDelay().empty()
                      ? llvmConstant(rewriter, location, i64, 0)
                      : adaptor.getDelay().front();
    Value staticSite = llvmConstant(
        rewriter, location, i64, staticallyStaged ? site.getId() : UINT64_MAX);
    Value clockingOutput =
        llvmConstant(rewriter, location, i64,
                     op.getClockingOutputAttr()
                         ? op.getClockingOutputAttr().getValue().getZExtValue()
                         : UINT64_MAX);
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_scheduler_packed_slice_nba"),
            ValueRange{runtimeContext,
                       LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                 "__obelisk_state_value"),
                       unknownPlane,
                       llvmConstant(rewriter, location, i64, stateBitCount),
                       base, llvmConstant(rewriter, location, i64, *baseWidth),
                       low.value, valid32,
                       llvmConstant(rewriter, location, i64, *sourceWidth),
                       delay, staticSite, clockingOutput, value, unknown})
            .getResult();
    LLVM::CallOp::create(rewriter, location, TypeRange{},
                         SymbolRefAttr::get(rewriter.getContext(),
                                            "obelisk_rt_v1_scheduler_fail"),
                         ValueRange{runtimeContext, status});
    if (guardContinuation) {
      cf::BranchOp::create(rewriter, location, guardContinuation);
      rewriter.setInsertionPointToStart(guardContinuation);
    }
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount = 0;
  const NativeStaticNBAPlan *staticPlan = nullptr;
  const NativeStateLayout *stateLayout = nullptr;
  bool staticSitesEnabled = false;
  bool guardedClaims = false;
};

class ImmediateNBAConversion final
    : public OpConversionPattern<sim::SimNBAEnqueueOp> {
public:
  ImmediateNBAConversion(const TypeConverter &converter, MLIRContext *context,
                         uint64_t stateBitCount,
                         const NativeStaticNBAPlan *staticPlan,
                         bool staticSitesEnabled, bool guardedClaims,
                         bool evalCeiling)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount),
        staticPlan(staticPlan), staticSitesEnabled(staticSitesEnabled),
        guardedClaims(guardedClaims), evalCeiling(evalCeiling) {}

  LogicalResult
  matchAndRewrite(sim::SimNBAEnqueueOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getDestination().size() != 1 || adaptor.getValue().empty())
      return failure();
    std::optional<unsigned> width = nativeStateWidth(op.getValue().getType());
    if (!width)
      return failure();
    Location location = op.getLoc();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Type i64 = rewriter.getI64Type();
    sim::SimFuncOp function = op->getParentOfType<sim::SimFuncOp>();
    bool driverDestination =
        isa<sim::DriverType>(op.getDestination().getType());
    bool inductiveTwoStateAccess =
        op->hasAttr("obelisk.eval.inductive_two_state_access");
    // Selected eval bodies execute only after their complete closure crosses
    // a known quiescent boundary, so they can use fixed root/region metadata.
    // That function-level fact does not prove exclusive ownership of an NBA
    // accumulator, however: a coincident four-state writer may have staged X
    // first. Only the per-access root proof may omit the zero unknown-plane
    // overwrite needed to preserve last-writer semantics.
    bool compactEvalMetadata =
        inductiveTwoStateAccess ||
        op->hasAttr(sim::metadata::evalCompactNBAMetadata);

    sim::NBASiteAttr site = op.getSiteAttr();
    auto staticRoot =
        site && staticPlan
            ? staticPlan->siteRoots.find(site.getId())
            : llvm::DenseMap<uint64_t, uint32_t>::const_iterator{};
    std::optional<uint64_t> destinationValue =
        resolveCFGConstantInteger(adaptor.getDestination().front());
    obelisk_rt_stable_handle_v1 decoded{};
    bool packedStaticStage =
        !op.getClockingOutputAttr() && !driverDestination && site &&
        staticPlan && staticRoot != staticPlan->siteRoots.end() &&
        staticRoot->second < staticPlan->roots.size() &&
        staticPlan->mergeSafeRoots[staticRoot->second] &&
        adaptor.getDelay().empty() && !site.getTiming() &&
        site.getStorage() != sim::ComputeNBAStorageKind::DynamicFrontier &&
        *width <= 64 && destinationValue &&
        obelisk_rt_stable_handle_decode(*destinationValue, &decoded) &&
        decoded.kind == OBELISK_RT_STABLE_HANDLE_STATIC &&
        decoded.id == staticPlan->roots[staticRoot->second].static_state &&
        decoded.offset >= 0 &&
        static_cast<uint64_t>(decoded.offset) <=
            staticPlan->roots[staticRoot->second].bit_width &&
        *width <= staticPlan->roots[staticRoot->second].bit_width -
                      static_cast<uint64_t>(decoded.offset) &&
        (llvm::all_of(
             adaptor.getValue(),
             [](Value value) { return isa<IntegerType>(value.getType()); }) ||
         (*width <= 64 && adaptor.getValue().size() == 1 &&
          isa<LLVM::LLVMPointerType>(adaptor.getValue().front().getType())));
    // Wide Eval sites need the per-site ordering/ingress preflight in AOT
    // materialization. Preserve their site identity in the static NBA ABI;
    // the ordinary actor still uses the existing accumulator/claim route.
    bool wideEvalSite = packedStaticStage && function &&
                        function->hasAttr("obelisk.eval.raw_captures") &&
                        staticPlan->roots[staticRoot->second].bit_width > 64;
    if (packedStaticStage && !wideEvalSite) {
      bool assumeClean = op->hasAttr(assumeCleanSpecializationAttr);
      StringRef generatedAccumulator =
          staticRoot->second < staticPlan->generatedAccumulators.size()
              ? staticPlan->generatedAccumulators[staticRoot->second]
              : StringRef{};
      bool useGuardedClaim =
          !assumeClean && (guardedClaims || generatedAccumulator.empty());
      auto widen = [&](Value value) {
        if (isa<LLVM::LLVMPointerType>(value.getType()))
          value = LLVM::LoadOp::create(
              rewriter, location,
              IntegerType::get(rewriter.getContext(), *width), value, 1);
        auto type = cast<IntegerType>(value.getType());
        return type.getWidth() == 64
                   ? value
                   : LLVM::ZExtOp::create(rewriter, location, i64, value)
                         .getResult();
      };
      Value unknown = llvmConstant(rewriter, location, i64, 0);
      if (adaptor.getValue().size() == 2)
        unknown = widen(adaptor.getValue()[1]);
      if (inductiveTwoStateAccess)
        unknown = llvmConstant(rewriter, location, i64, 0);
      Value value = widen(adaptor.getValue().front());
      uint32_t homeRegion =
          function ? getRuntimeEventRegion(function.getHomeRegion())
                   : UINT32_MAX;
      uint32_t commitRegion = homeRegion == OBELISK_RT_REGION_ACTIVE ||
                                      homeRegion == OBELISK_RT_REGION_REACTIVE
                                  ? homeRegion + 2
                                  : UINT32_MAX;
      uint64_t rootWidth = staticPlan->roots[staticRoot->second].bit_width;
      bool directLaneStage = !generatedAccumulator.empty() && *width == 32 &&
                             adaptor.getValue().size() == 1 &&
                             (decoded.offset & 31) == 0;
      bool directPartialScalarStage =
          !generatedAccumulator.empty() && rootWidth <= 64;
      bool directGeneratedStage = commitRegion != UINT32_MAX &&
                                  (directPartialScalarStage || directLaneStage);
      auto emitDirectGeneratedStage = [&] {
        bool fixedRegionEvalStage =
            compactEvalMetadata &&
            staticRoot->second < staticPlan->generatedCommitRegions.size() &&
            staticPlan->generatedCommitRegions[staticRoot->second] !=
                UINT32_MAX;
        bool fullRootEvalStage =
            fixedRegionEvalStage &&
            staticRoot->second < staticPlan->generatedFullRootStages.size() &&
            staticPlan->generatedFullRootStages[staticRoot->second];
        uint64_t fixedWriteMask =
            staticRoot->second < staticPlan->generatedFixedWriteMasks.size()
                ? staticPlan->generatedFixedWriteMasks[staticRoot->second]
                : 0;
        Value base = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                               generatedAccumulator);
        if (directPartialScalarStage) {
          uint64_t sourceMask =
              *width == 64 ? UINT64_MAX : (uint64_t{1} << *width) - 1;
          uint64_t mask = sourceMask << decoded.offset;
          auto mergeField = [&](size_t fieldOffset, Value fieldValue) {
            Value address = byteGEP(rewriter, location, base, fieldOffset);
            Value positioned = fieldValue;
            if (decoded.offset != 0)
              positioned = arith::ShLIOp::create(
                  rewriter, location, positioned,
                  llvmConstant(rewriter, location, i64, decoded.offset));
            Value masked = arith::AndIOp::create(
                rewriter, location, positioned,
                llvmConstant(rewriter, location, i64, mask));
            Value merged = masked;
            if (!compactEvalMetadata || fixedWriteMask == 0) {
              Value old =
                  LLVM::LoadOp::create(rewriter, location, i64, address, 8);
              merged = arith::OrIOp::create(
                  rewriter, location,
                  arith::AndIOp::create(
                      rewriter, location, old,
                      llvmConstant(rewriter, location, i64, ~mask)),
                  masked);
            }
            LLVM::StoreOp::create(rewriter, location, merged, address, 8);
          };
          if (staticRoot->second < staticPlan->trackTransients.size() &&
              staticPlan->trackTransients[staticRoot->second]) {
            auto position = [&](Value fieldValue) -> Value {
              Value positioned = fieldValue;
              if (decoded.offset != 0)
                positioned = arith::ShLIOp::create(
                    rewriter, location, positioned,
                    llvmConstant(rewriter, location, i64, decoded.offset));
              return arith::AndIOp::create(
                  rewriter, location, positioned,
                  llvmConstant(rewriter, location, i64, mask));
            };
            // A compact stage does not maintain write_mask, and a compact
            // and a full body may stage one root in the same barrier. When
            // every write to the root covers the same bits, the dirty bit that
            // both set says whether this barrier already staged them.
            bool planFullRoot =
                staticRoot->second <
                    staticPlan->generatedFullRootStages.size() &&
                staticPlan->generatedFullRootStages[staticRoot->second];
            Value staged;
            if (planFullRoot || fixedWriteMask != 0) {
              Value dirty = LLVM::LoadOp::create(
                  rewriter, location, i64,
                  byteGEP(rewriter, location,
                          LLVM::AddressOfOp::create(
                              rewriter, location, pointer,
                              "__obelisk_aot_nba_dirty_roots_v1"),
                          static_cast<uint64_t>(staticRoot->second / 64) *
                              sizeof(uint64_t)),
                  8);
              Value bit = arith::AndIOp::create(
                  rewriter, location, dirty,
                  llvmConstant(rewriter, location, i64,
                               uint64_t{1} << (staticRoot->second % 64)));
              staged = arith::SelectOp::create(
                  rewriter, location,
                  arith::CmpIOp::create(
                      rewriter, location, arith::CmpIPredicate::ne, bit,
                      llvmConstant(rewriter, location, i64, 0)),
                  llvmConstant(rewriter, location, i64, mask),
                  llvmConstant(rewriter, location, i64, 0));
            }
            emitGeneratedNBATransient(
                rewriter, location, base, 0,
                llvmConstant(rewriter, location, i64, mask), position(value),
                position(unknown), staged);
          }
          mergeField(offsetof(obelisk_rt_generated_nba_accumulator_256, value),
                     value);
          // A promoted body can share the generated accumulator with a
          // four-state bootstrap or fallback body.  Explicitly overwrite its
          // selected unknown bits with zero: otherwise a later mixed or
          // checkpoint barrier can replay stale X/Z data from the prior
          // phase into canonical state.
          mergeField(
              offsetof(obelisk_rt_generated_nba_accumulator_256, unknown),
              unknown);
          if (!compactEvalMetadata ||
              (!fullRootEvalStage && fixedWriteMask == 0)) {
            Value maskAddress = byteGEP(
                rewriter, location, base,
                offsetof(obelisk_rt_generated_nba_accumulator_256, write_mask));
            Value oldMask =
                LLVM::LoadOp::create(rewriter, location, i64, maskAddress, 8);
            LLVM::StoreOp::create(
                rewriter, location,
                arith::OrIOp::create(
                    rewriter, location, oldMask,
                    llvmConstant(rewriter, location, i64, mask)),
                maskAddress, 8);
          }
        } else {
          uint64_t laneOffset = static_cast<uint64_t>(decoded.offset / 8);
          Value laneValue =
              LLVM::TruncOp::create(rewriter, location, i32, value);
          LLVM::StoreOp::create(
              rewriter, location, laneValue,
              byteGEP(
                  rewriter, location, base,
                  offsetof(obelisk_rt_generated_nba_accumulator_256, value) +
                      laneOffset),
              4);
          // This lane form is restricted to two-state values, but the same
          // accumulator can have been populated by a four-state phase before
          // promotion.  Clear the selected unknown lane as part of staging so
          // every legal barrier observes the same known value.
          LLVM::StoreOp::create(
              rewriter, location,
              llvmConstant(rewriter, location, i32, UINT32_MAX),
              byteGEP(rewriter, location, base,
                      offsetof(obelisk_rt_generated_nba_accumulator_256,
                               write_mask) +
                          laneOffset),
              4);
          LLVM::StoreOp::create(
              rewriter, location, llvmConstant(rewriter, location, i32, 0),
              byteGEP(
                  rewriter, location, base,
                  offsetof(obelisk_rt_generated_nba_accumulator_256, unknown) +
                      laneOffset),
              4);
        }
        if (!fixedRegionEvalStage) {
          LLVM::StoreOp::create(
              rewriter, location, llvmConstant(rewriter, location, i32, 1),
              byteGEP(
                  rewriter, location, base,
                  offsetof(obelisk_rt_generated_nba_accumulator_256, valid)),
              4);
          LLVM::StoreOp::create(
              rewriter, location,
              llvmConstant(rewriter, location, i32, commitRegion),
              byteGEP(rewriter, location, base,
                      offsetof(obelisk_rt_generated_nba_accumulator_256,
                               exec_region)),
              4);
        }
        Value dirtyBase = LLVM::AddressOfOp::create(
            rewriter, location, pointer, "__obelisk_aot_nba_dirty_roots_v1");
        Value dirtyWord = byteGEP(
            rewriter, location, dirtyBase,
            static_cast<uint64_t>(staticRoot->second / 64) * sizeof(uint64_t));
        Value previous =
            LLVM::LoadOp::create(rewriter, location, i64, dirtyWord, 8);
        Value marked = LLVM::OrOp::create(
            rewriter, location, previous,
            llvmConstant(rewriter, location, i64,
                         uint64_t{1} << (staticRoot->second % 64)));
        LLVM::StoreOp::create(rewriter, location, marked, dirtyWord, 8);
        if (!compactEvalMetadata) {
          Value summaryBase =
              LLVM::AddressOfOp::create(rewriter, location, pointer,
                                        "__obelisk_aot_nba_dirty_summary_v1");
          uint32_t dirtyWordIndex = staticRoot->second / 64;
          Value summaryWord = byteGEP(
              rewriter, location, summaryBase,
              static_cast<uint64_t>(dirtyWordIndex / 64) * sizeof(uint64_t));
          Value previousSummary =
              LLVM::LoadOp::create(rewriter, location, i64, summaryWord, 8);
          Value markedSummary = LLVM::OrOp::create(
              rewriter, location, previousSummary,
              llvmConstant(rewriter, location, i64,
                           uint64_t{1} << (dirtyWordIndex % 64)));
          LLVM::StoreOp::create(rewriter, location, markedSummary, summaryWord,
                                8);
        }
      };
      if (!useGuardedClaim && directGeneratedStage) {
        emitDirectGeneratedStage();
        rewriter.eraseOp(op);
        return success();
      }
      if (useGuardedClaim && directGeneratedStage) {
        Value useDirect = staticNBASpecializationGuard(rewriter, location,
                                                       staticRoot->second);
        Block *head = rewriter.getInsertionBlock();
        Block *continuation =
            rewriter.splitBlock(head, rewriter.getInsertionPoint());
        Region *region = head->getParent();
        Block *directBlock =
            rewriter.createBlock(region, continuation->getIterator());
        Block *claimBlock =
            rewriter.createBlock(region, continuation->getIterator());
        recordStaticSpecializationCFGBlocks(rewriter, head, 3);

        rewriter.setInsertionPointToEnd(head);
        markLikelyTrue(cf::CondBranchOp::create(rewriter, location, useDirect,
                                                directBlock, ValueRange{},
                                                claimBlock, ValueRange{}));

        rewriter.setInsertionPointToEnd(directBlock);
        emitDirectGeneratedStage();
        cf::BranchOp::create(rewriter, location, continuation);

        rewriter.setInsertionPointToEnd(claimBlock);
        Value contextAddress = LLVM::AddressOfOp::create(
            rewriter, location, pointer, "__obelisk_current_context");
        Value runtimeContext = LLVM::LoadOp::create(rewriter, location, pointer,
                                                    contextAddress, 8);
        Value status =
            LLVM::CallOp::create(
                rewriter, location, TypeRange{i32},
                SymbolRefAttr::get(rewriter.getContext(),
                                   "obelisk_rt_v1_static_nba_claim"),
                ValueRange{
                    runtimeContext,
                    llvmConstant(rewriter, location, i32, staticRoot->second),
                    LLVM::AddressOfOp::create(rewriter, location, pointer,
                                              "__obelisk_state_value"),
                    LLVM::ZeroOp::create(rewriter, location, pointer),
                    llvmConstant(rewriter, location, i64, stateBitCount),
                    llvmConstant(rewriter, location, i64,
                                 static_cast<uint64_t>(decoded.offset)),
                    llvmConstant(rewriter, location, i64, *width), value,
                    unknown})
                .getResult();
        LLVM::CallOp::create(rewriter, location, TypeRange{},
                             SymbolRefAttr::get(rewriter.getContext(),
                                                "obelisk_rt_v1_scheduler_fail"),
                             ValueRange{runtimeContext, status});
        cf::BranchOp::create(rewriter, location, continuation);

        rewriter.setInsertionPointToStart(continuation);
        rewriter.eraseOp(op);
        return success();
      }
      Value contextAddress = LLVM::AddressOfOp::create(
          rewriter, location, pointer, "__obelisk_current_context");
      Value runtimeContext =
          LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
      if (!useGuardedClaim) {
        LLVM::CallOp::create(
            rewriter, location, TypeRange{},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_static_nba_stage_wide"),
            ValueRange{
                runtimeContext,
                llvmConstant(rewriter, location, i32, staticRoot->second),
                llvmConstant(rewriter, location, i64,
                             static_cast<uint64_t>(decoded.offset)),
                llvmConstant(rewriter, location, i64, *width), value, unknown,
                llvmConstant(rewriter, location, i32,
                             adaptor.getValue().size() == 2 ? 1 : 0)});
        rewriter.eraseOp(op);
        return success();
      }
      Value status =
          LLVM::CallOp::create(
              rewriter, location, TypeRange{i32},
              SymbolRefAttr::get(rewriter.getContext(),
                                 "obelisk_rt_v1_static_nba_claim"),
              ValueRange{
                  runtimeContext,
                  llvmConstant(rewriter, location, i32, staticRoot->second),
                  LLVM::AddressOfOp::create(rewriter, location, pointer,
                                            "__obelisk_state_value"),
                  adaptor.getValue().size() == 2
                      ? LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                  "__obelisk_state_unknown")
                            .getResult()
                      : LLVM::ZeroOp::create(rewriter, location, pointer)
                            .getResult(),
                  llvmConstant(rewriter, location, i64, stateBitCount),
                  llvmConstant(rewriter, location, i64,
                               static_cast<uint64_t>(decoded.offset)),
                  llvmConstant(rewriter, location, i64, *width), value,
                  unknown})
              .getResult();
      LLVM::CallOp::create(rewriter, location, TypeRange{},
                           SymbolRefAttr::get(rewriter.getContext(),
                                              "obelisk_rt_v1_scheduler_fail"),
                           ValueRange{runtimeContext, status});
      rewriter.eraseOp(op);
      return success();
    }

    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value runtimeContext =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    auto savePlane = [&](Value value) {
      Value address = entryAlloca(rewriter, location, value.getType(), 1, 1);
      LLVM::StoreOp::create(rewriter, location, value, address, 1);
      return address;
    };
    Value value = savePlane(adaptor.getValue().front());
    Value unknown = LLVM::ZeroOp::create(rewriter, location, pointer);
    Value unknownPlane = LLVM::ZeroOp::create(rewriter, location, pointer);
    if (adaptor.getValue().size() == 2) {
      unknown = savePlane(adaptor.getValue()[1]);
      unknownPlane = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                               "__obelisk_state_unknown");
    }
    Value valuePlane = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                                 "__obelisk_state_value");
    Value delay = adaptor.getDelay().empty()
                      ? llvmConstant(rewriter, location, i64, 0)
                      : adaptor.getDelay().front();
    if (isa<sim::StringType>(op.getValue().getType())) {
      Value status =
          LLVM::CallOp::create(
              rewriter, location, TypeRange{i32},
              SymbolRefAttr::get(rewriter.getContext(),
                                 "obelisk_rt_v1_scheduler_string_nba"),
              ValueRange{runtimeContext, valuePlane,
                         llvmConstant(rewriter, location, i64, stateBitCount),
                         adaptor.getDestination().front(), delay,
                         adaptor.getValue().front()})
              .getResult();
      LLVM::CallOp::create(rewriter, location, TypeRange{},
                           SymbolRefAttr::get(rewriter.getContext(),
                                              "obelisk_rt_v1_scheduler_fail"),
                           ValueRange{runtimeContext, status});
    } else {
      bool mappedSite = site && staticPlan &&
                        staticPlan->siteRoots.contains(site.getId());
      uint32_t rootIndex =
          mappedSite ? staticPlan->siteRoots.lookup(site.getId()) : UINT32_MAX;
      bool orderedEvalQueue =
          function && function->hasAttr("obelisk.eval.ordered_nba_queue");
      bool staticallyStaged =
          !op.getClockingOutputAttr() && !driverDestination &&
          staticSitesEnabled && mappedSite &&
          (staticPlan->mergeSafeRoots[rootIndex] || orderedEvalQueue) &&
          adaptor.getDelay().empty() && !site.getTiming() &&
          site.getStorage() != sim::ComputeNBAStorageKind::DynamicFrontier;
      SmallVector<Value> arguments{
          runtimeContext,
          valuePlane,
          unknownPlane,
          llvmConstant(rewriter, location, i64, stateBitCount),
          adaptor.getDestination().front(),
          llvmConstant(rewriter, location, i64, *width)};
      if (staticallyStaged)
        arguments.insert(arguments.begin() + 1,
                         llvmConstant(rewriter, location, i64, site.getId()));
      else
        arguments.push_back(delay);
      arguments.push_back(value);
      arguments.push_back(unknown);
      if (auto clockingOutput = op.getClockingOutputAttr())
        arguments.push_back(llvmConstant(
            rewriter, location, i64, clockingOutput.getValue().getZExtValue()));
      Value status =
          LLVM::CallOp::create(
              rewriter, location, TypeRange{i32},
              SymbolRefAttr::get(
                  rewriter.getContext(),
                  op.getClockingOutputAttr()
                      ? driverDestination
                            ? "obelisk_rt_v1_scheduler_clocking_driver_nba"
                            : "obelisk_rt_v1_scheduler_clocking_nba"
                  : driverDestination ? "obelisk_rt_v1_scheduler_driver_nba"
                  : staticallyStaged  ? "obelisk_rt_v1_scheduler_static_nba"
                                      : "obelisk_rt_v1_scheduler_nba"),
              arguments)
              .getResult();
      LLVM::CallOp::create(rewriter, location, TypeRange{},
                           SymbolRefAttr::get(rewriter.getContext(),
                                              "obelisk_rt_v1_scheduler_fail"),
                           ValueRange{runtimeContext, status});
    }
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount;
  const NativeStaticNBAPlan *staticPlan;
  bool staticSitesEnabled;
  bool guardedClaims;
  bool evalCeiling;
};

} // namespace

void emitGeneratedNBATransient(OpBuilder &builder, Location location,
                               Value accumulator, uint64_t word, Value mask,
                               Value newValue, Value newUnknown,
                               Value staged) {
  Type i64 = builder.getI64Type();
  auto field = [&](size_t offset) {
    return byteGEP(builder, location, accumulator,
                   offset + word * sizeof(uint64_t));
  };
  auto load = [&](size_t offset) {
    return LLVM::LoadOp::create(builder, location, i64, field(offset), 8)
        .getResult();
  };
  if (!staged)
    staged = arith::AndIOp::create(
        builder, location,
        load(offsetof(obelisk_rt_generated_nba_accumulator_256, write_mask)),
        mask);
  Value differs = arith::OrIOp::create(
      builder, location,
      arith::XOrIOp::create(
          builder, location,
          load(offsetof(obelisk_rt_generated_nba_accumulator_256, value)),
          newValue),
      arith::XOrIOp::create(
          builder, location,
          load(offsetof(obelisk_rt_generated_nba_accumulator_256, unknown)),
          newUnknown));
  Value transientAddress =
      field(offsetof(obelisk_rt_generated_nba_accumulator_256, transient));
  Value transient =
      LLVM::LoadOp::create(builder, location, i64, transientAddress, 8);
  LLVM::StoreOp::create(
      builder, location,
      arith::OrIOp::create(
          builder, location, transient,
          arith::AndIOp::create(builder, location, staged, differs)),
      transientAddress, 8);
}

void populateNBAToLLVMConversionPatterns(RewritePatternSet &patterns,
                                         TypeConverter &converter,
                                         uint64_t stateBitCount,
                                         const NativeStaticNBAPlan *staticPlan,
                                         const NativeStateLayout *stateLayout,
                                         bool staticSitesEnabled,
                                         bool guardedClaims, bool evalCeiling) {
  patterns.add<InertialDriverConversion>(converter, patterns.getContext(),
                                         stateBitCount);
  patterns.add<InertialPathDriverConversion>(converter, patterns.getContext(),
                                             stateBitCount);
  patterns.add<InertialPathStorageConversion>(converter, patterns.getContext(),
                                              stateBitCount);
  patterns.add<InertialStrengthPairConversion>(converter, patterns.getContext(),
                                               stateBitCount);
  patterns.add<InertialPathStrengthPairConversion>(
      converter, patterns.getContext(), stateBitCount);
  patterns.add<PackedSliceNBAConversion>(converter, patterns.getContext(),
                                         stateBitCount, staticPlan, stateLayout,
                                         staticSitesEnabled, guardedClaims);
  patterns.add<ImmediateNBAConversion>(
      converter, patterns.getContext(), stateBitCount, staticPlan,
      staticSitesEnabled, guardedClaims, evalCeiling);
}

} // namespace obelisk::detail
