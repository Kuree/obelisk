//===- SimulationStateAccessLowering.cpp - Native state access patterns --===//

#include "SimulationToLLVMCoroutinePrivate.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;

namespace obelisk::detail {
namespace {

constexpr StringLiteral continuousStoreAttrName =
    "obelisk_sim.continuous_store";

bool useTwoStateSpecialization(Operation *operation, bool moduleWide) {
  if (moduleWide)
    return true;
  return operation->hasAttr("obelisk.eval.inductive_two_state_access");
}

class RefLoadConversion final : public OpConversionPattern<sim::SimRefLoadOp> {
public:
  RefLoadConversion(const TypeConverter &converter, MLIRContext *context,
                    uint64_t stateBitCount,
                    const NativeStateLayout *directLayout,
                    bool experimentalTwoState)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount),
        directLayout(directLayout), experimentalTwoState(experimentalTwoState) {}

  LogicalResult
  matchAndRewrite(sim::SimRefLoadOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = op.getResult().getType();
    bool twoState = useTwoStateSpecialization(op, experimentalTwoState);
    std::optional<unsigned> width = nativeStateWidth(resultType);
    if (!width || adaptor.getReference().size() != 1)
      return failure();
    IntegerType plane = rewriter.getIntegerType(*width);
    bool assumeClean = op->hasAttr(assumeCleanSpecializationAttr);
    Value guardedPermission;
    if (auto range = resolveDirectStaticStateRange(
            adaptor.getReference().front(), *width, directLayout);
        range && range->guarded && !assumeClean)
      guardedPermission = staticSpecializationGuard(
          rewriter, op.getLoc(), range->staticID, OBELISK_RT_STATIC_ROOT_READ);
    Value value =
        loadStatePlane(rewriter, op.getLoc(), adaptor.getReference().front(),
                       plane, "__obelisk_state_value", false, stateBitCount,
                       directLayout, guardedPermission, assumeClean);
    if (isa<FloatType>(resultType))
      value =
          arith::BitcastOp::create(rewriter, op.getLoc(), resultType, value);
    SmallVector<Value> converted{value};
    if (containsLogic(resultType)) {
      Value unknown = twoState
                          ? llvmConstant(rewriter, op.getLoc(), plane, 0)
                          : loadStatePlane(
                                rewriter, op.getLoc(),
                                adaptor.getReference().front(), plane,
                                "__obelisk_state_unknown", true, stateBitCount,
                                directLayout, guardedPermission, assumeClean);
      converted.push_back(unknown);
    }
    SmallVector<ValueRange> replacements{ValueRange(converted)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }

private:
  uint64_t stateBitCount;
  const NativeStateLayout *directLayout;
  bool experimentalTwoState;
};

class RefStoreConversion final
    : public OpConversionPattern<sim::SimRefStoreOp> {
public:
  RefStoreConversion(const TypeConverter &converter, MLIRContext *context,
                     uint64_t stateBitCount,
                     const NativeStateLayout *directLayout,
                     bool experimentalTwoState)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount),
        directLayout(directLayout), experimentalTwoState(experimentalTwoState) {}

  LogicalResult
  matchAndRewrite(sim::SimRefStoreOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getReference().size() != 1 || adaptor.getValue().empty())
      return failure();
    Type valueType = op.getValue().getType();
    bool twoState = useTwoStateSpecialization(op, experimentalTwoState);
    std::optional<unsigned> width = nativeStateWidth(valueType);
    if (!width)
      return failure();
    IntegerType plane = rewriter.getIntegerType(*width);
    bool assumeClean = op->hasAttr(assumeCleanSpecializationAttr);
    sim::SimFuncOp function = op->getParentOfType<sim::SimFuncOp>();
    sim::EntryKind entryKind = function.getEntryKind();
    bool continuous = op->hasAttr(continuousStoreAttrName) ||
                      entryKind == sim::EntryKind::Continuous ||
                      entryKind == sim::EntryKind::PortInput ||
                      entryKind == sim::EntryKind::PortOutput;
    std::optional<DirectStaticStateRange> directRange =
        resolveDirectStaticStateRange(adaptor.getReference().front(), *width,
                                      directLayout);
    bool needsNotification = true;
    // Exact fanout proves that an absent root has no language-level waiter.
    // Direct roots are also immune to external writes (VPI-off/read), while a
    // guarded VPI-full root may elide observers only in its clean fast body.
    if (directLayout && directLayout->transitionHandlesExact)
      if (directRange && (assumeClean || !directRange->guarded))
        needsNotification =
            directLayout->transitionHandles.contains(directRange->staticID);
    Value guardedPermission;
    if (directRange && directRange->guarded && !assumeClean)
      guardedPermission = staticSpecializationGuard(
          rewriter, op.getLoc(), directRange->staticID,
          OBELISK_RT_STATIC_ROOT_READ | OBELISK_RT_STATIC_ROOT_WRITE);
    Value storedValue = adaptor.getValue().front();
    if (isa<FloatType>(valueType))
      storedValue =
          arith::BitcastOp::create(rewriter, op.getLoc(), plane, storedValue);
    if (!needsNotification) {
      (void)storeStatePlane(
          rewriter, op.getLoc(), adaptor.getReference().front(), storedValue,
          "__obelisk_state_value", stateBitCount, directLayout,
          guardedPermission, assumeClean, /*trackChange=*/false, continuous);
      if (adaptor.getValue().size() == 2 && !twoState)
        (void)storeStatePlane(
            rewriter, op.getLoc(), adaptor.getReference().front(),
            adaptor.getValue()[1], "__obelisk_state_unknown", stateBitCount,
            directLayout, guardedPermission, assumeClean,
            /*trackChange=*/false, continuous);
      rewriter.eraseOp(op);
      return success();
    }
    Value oldValue =
        loadStatePlane(rewriter, op.getLoc(), adaptor.getReference().front(),
                       plane, "__obelisk_state_value", false, stateBitCount,
                       directLayout, guardedPermission, assumeClean);
    Value oldUnknown;
    if (containsLogic(valueType))
      oldUnknown = twoState
                       ? llvmConstant(rewriter, op.getLoc(), plane, 0)
                       : loadStatePlane(
                             rewriter, op.getLoc(),
                             adaptor.getReference().front(), plane,
                             "__obelisk_state_unknown", true, stateBitCount,
                             directLayout, guardedPermission, assumeClean);
    Value notificationValue = storedValue;
    Value notificationUnknown = adaptor.getValue().size() == 2 && !twoState
                                    ? adaptor.getValue()[1]
                                    : Value{};
    if (isa<sim::StringType>(valueType)) {
      Value comparison =
          LLVM::CallOp::create(
              rewriter, op.getLoc(), TypeRange{rewriter.getI32Type()},
              SymbolRefAttr::get(rewriter.getContext(),
                                 "obelisk_rt_v1_string_compare"),
              ValueRange{oldValue, storedValue})
              .getResult();
      Value equal = arith::CmpIOp::create(
          rewriter, op.getLoc(), arith::CmpIPredicate::eq, comparison,
          llvmConstant(rewriter, op.getLoc(), rewriter.getI32Type(), 0));
      notificationValue = arith::SelectOp::create(rewriter, op.getLoc(), equal,
                                                  oldValue, storedValue);
    }
    Value valueChanged =
        storeStatePlane(rewriter, op.getLoc(), adaptor.getReference().front(),
                        storedValue, "__obelisk_state_value", stateBitCount,
                        directLayout, guardedPermission, assumeClean,
                        /*trackChange=*/true, continuous);
    // Runtime plane stores can suppress a write while force or procedural
    // assign owns the variable. Publish the value that actually became
    // visible, not the attempted procedural assignment (IEEE 1800-2017
    // 10.6.1-10.6.2). A no-change store likewise retains the already loaded
    // value, so the same selects cover both cases without another plane load.
    notificationValue = arith::SelectOp::create(
        rewriter, op.getLoc(), valueChanged, notificationValue, oldValue);
    if (adaptor.getValue().size() == 2 && !twoState) {
      Value unknownChanged = storeStatePlane(
          rewriter, op.getLoc(), adaptor.getReference().front(),
          adaptor.getValue()[1], "__obelisk_state_unknown", stateBitCount,
          directLayout, guardedPermission, assumeClean,
          /*trackChange=*/true, continuous);
      notificationUnknown = arith::SelectOp::create(
          rewriter, op.getLoc(), unknownChanged, adaptor.getValue()[1],
          oldUnknown);
    }
    // A generic packed store can update only the currently unmasked bits.
    // Reload its canonical result so partial external forces cannot leak the
    // attempted value through transition publication. Direct clean stores
    // have no override mask and keep the select-only fast path above.
    bool needsVisibleReload =
        sim::getPackedWidth(valueType).has_value() &&
        (continuous || !directLayout || !directRange ||
         (directRange->guarded && !assumeClean));
    if (needsVisibleReload) {
      notificationValue = loadStatePlane(
          rewriter, op.getLoc(), adaptor.getReference().front(), plane,
          "__obelisk_state_value", false, stateBitCount, directLayout,
          guardedPermission, assumeClean);
      if (containsLogic(valueType))
        notificationUnknown =
            twoState
                ? llvmConstant(rewriter, op.getLoc(), plane, 0)
                : loadStatePlane(rewriter, op.getLoc(),
                                 adaptor.getReference().front(), plane,
                                 "__obelisk_state_unknown", true,
                                 stateBitCount, directLayout,
                                 guardedPermission, assumeClean);
    }
    if (isa<FloatType>(valueType)) {
      Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
      auto save = [&](Value value) {
        Value storage =
            entryAlloca(rewriter, op.getLoc(), value.getType(), 1, 1);
        LLVM::StoreOp::create(rewriter, op.getLoc(), value, storage, 1);
        return storage;
      };
      Value contextAddress = LLVM::AddressOfOp::create(
          rewriter, op.getLoc(), pointer, "__obelisk_current_context");
      Value runtimeContext = LLVM::LoadOp::create(rewriter, op.getLoc(),
                                                  pointer, contextAddress, 8);
      LLVM::CallOp::create(
          rewriter, op.getLoc(), TypeRange{},
          SymbolRefAttr::get(rewriter.getContext(),
                             "obelisk_rt_v1_scheduler_real_transition"),
          ValueRange{runtimeContext, adaptor.getReference().front(),
                     llvmConstant(rewriter, op.getLoc(), rewriter.getI32Type(),
                                  *width),
                     save(oldValue), save(notificationValue)});
    } else {
      notifySignal(rewriter, op.getLoc(), adaptor.getReference().front(),
                   *width, oldValue, oldUnknown, notificationValue,
                   notificationUnknown,
                   directRange && (assumeClean || !directRange->guarded) &&
                           directLayout && directLayout->transitionHandlesExact
                       ? directRange
                       : std::nullopt);
    }
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount;
  const NativeStateLayout *directLayout;
  bool experimentalTwoState;
};

class NetReadConversion final : public OpConversionPattern<sim::SimNetReadOp> {
public:
  NetReadConversion(const TypeConverter &converter, MLIRContext *context,
                    uint64_t stateBitCount,
                    const NativeStateLayout *directLayout,
                    bool experimentalTwoState)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount),
        directLayout(directLayout), experimentalTwoState(experimentalTwoState) {}

  LogicalResult
  matchAndRewrite(sim::SimNetReadOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = op.getResult().getType();
    bool twoState = useTwoStateSpecialization(op, experimentalTwoState);
    std::optional<unsigned> width = nativeStateWidth(resultType);
    if (!width || adaptor.getNet().size() != 1)
      return failure();
    IntegerType plane = rewriter.getIntegerType(*width);
    SmallVector<Value> converted{
        loadStatePlane(rewriter, op.getLoc(), adaptor.getNet().front(), plane,
                       "__obelisk_state_value", false, stateBitCount,
                       directLayout)};
    if (containsLogic(resultType)) {
      Value unknown = twoState
                          ? llvmConstant(rewriter, op.getLoc(), plane, 0)
                          : loadStatePlane(
                                rewriter, op.getLoc(), adaptor.getNet().front(),
                                plane, "__obelisk_state_unknown", true,
                                stateBitCount, directLayout);
      converted.push_back(unknown);
    }
    SmallVector<ValueRange> replacements{ValueRange(converted)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }

private:
  uint64_t stateBitCount;
  const NativeStateLayout *directLayout;
  bool experimentalTwoState;
};

} // namespace

void populateStateReadWriteToLLVMConversionPatterns(
    RewritePatternSet &patterns, TypeConverter &converter,
    uint64_t stateBitCount, const NativeStateLayout *directLayout,
    bool experimentalTwoState) {
  patterns.add<RefLoadConversion, RefStoreConversion>(
      converter, patterns.getContext(), stateBitCount, directLayout,
      experimentalTwoState);
  patterns.add<NetReadConversion>(converter, patterns.getContext(),
                                  stateBitCount, directLayout,
                                  experimentalTwoState);
}

} // namespace obelisk::detail
