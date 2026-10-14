//===- SimulationStateAccessLowering.cpp - Native state access patterns --===//

#include "SimulationLocalStorageLowering.h"
#include "SimulationToLLVMCoroutinePrivate.h"
#include "obelisk/Analysis/NativeStorageLayout.h"
#include "obelisk/Dialect/Native/NativeDialect.h"
#include "obelisk/Dialect/Schedule/ScheduleEnums.h"
#include "obelisk/Dialect/Schedule/ScheduleFields.h"
#include "obelisk/Dialect/Schedule/ScheduleMetadata.h"
#include "obelisk/Dialect/Schedule/ScheduleOps.h"

#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;

namespace obelisk::detail {
namespace {

constexpr StringLiteral continuousStoreAttrName = "simulation.continuous_store";

// Canonical padding is not a language value. Invalid aggregate reads also
// need the per-member defaults from IEEE 1800-2023 Table 7-1.
Value maskStoragePlane(OpBuilder &builder, Location location, Value bits,
                       Type type, bool unknown) {
  APInt mask = unknown ? analysis::getDefaultUnknownMask(type)
                       : analysis::getLogicalStorageMask(type);
  if (mask.isAllOnes())
    return bits;
  return arith::AndIOp::create(
      builder, location, bits,
      arith::ConstantOp::create(builder, location,
                                builder.getIntegerAttr(bits.getType(), mask)));
}

Value loadCurrentRuntimeContext(ConversionPatternRewriter &rewriter,
                                Location location) {
  Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
  Value address = LLVM::AddressOfOp::create(rewriter, location, pointer,
                                            "__obelisk_current_context");
  return LLVM::LoadOp::create(rewriter, location, pointer, address, 8);
}

void reportRuntimeControlStatus(ConversionPatternRewriter &rewriter,
                                Location location, Value context,
                                Value status) {
  LLVM::CallOp::create(
      rewriter, location, TypeRange{},
      SymbolRefAttr::get(rewriter.getContext(), "obelisk_rt_v1_scheduler_fail"),
      ValueRange{context, status});
}

bool useTwoStateSpecialization(Operation *operation, bool moduleWide) {
  if (moduleWide)
    return true;
  return ::obelisk::schedule::has<
      ::obelisk::schedule::Field::EvalInductiveTwoStateAccess>(operation);
}

/// A read whose location is known on its executing path observes a zero
/// unknown plane; only its value plane needs to be loaded.
bool readsKnownLocation(Operation *operation, bool moduleWide) {
  return useTwoStateSpecialization(operation, moduleWide) ||
         ::obelisk::schedule::has<::obelisk::schedule::Field::NativeKnownRead>(
             operation);
}

Value allocateBulkPlane(ConversionPatternRewriter &rewriter, Location location,
                        uint64_t width) {
  return entryAlloca(rewriter, location, rewriter.getI8Type(), (width + 7) / 8,
                     1);
}

Value loadBulkStatePlane(ConversionPatternRewriter &rewriter, Location location,
                         Value handle, uint64_t width, StringRef globalName,
                         bool unknownFallback, uint64_t stateBitCount,
                         const NativeStateLayout *directLayout,
                         bool assumeClean) {
  Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
  Type i32 = rewriter.getI32Type();
  Type i64 = rewriter.getI64Type();
  Value output = allocateBulkPlane(rewriter, location, width);
  std::optional<DirectStaticStateRange> range =
      resolveDirectStaticStateRange(handle, width, directLayout);
  if (range && (!range->guarded || assumeClean) && range->offset % 8 == 0) {
    Value base =
        LLVM::AddressOfOp::create(rewriter, location, pointer, globalName);
    LLVM::MemcpyOp::create(
        rewriter, location, output,
        byteGEP(rewriter, location, base, range->offset / 8),
        llvmConstant(rewriter, location, i64, (width + 7) / 8), false);
    return output;
  }
  Value base =
      LLVM::AddressOfOp::create(rewriter, location, pointer, globalName);
  Value context = loadCurrentRuntimeContext(rewriter, location);
  LLVM::CallOp::create(
      rewriter, location, TypeRange{i32},
      SymbolRefAttr::get(rewriter.getContext(),
                         "obelisk_rt_v1_native_state_load_plane"),
      ValueRange{context, base,
                 llvmConstant(rewriter, location, i64, stateBitCount), handle,
                 llvmConstant(rewriter, location, i64, width),
                 llvmConstant(rewriter, location, i32,
                              globalName == "__obelisk_state_unknown" ? 1 : 0),
                 llvmConstant(rewriter, location, i32, unknownFallback ? 1 : 0),
                 output});
  return output;
}

void storeBulkStatePlane(ConversionPatternRewriter &rewriter, Location location,
                         Value handle, Value input, uint64_t width,
                         StringRef globalName, uint64_t stateBitCount,
                         Value changed, bool continuous) {
  Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
  Type i32 = rewriter.getI32Type();
  Type i64 = rewriter.getI64Type();
  Value base =
      LLVM::AddressOfOp::create(rewriter, location, pointer, globalName);
  Value context = loadCurrentRuntimeContext(rewriter, location);
  LLVM::CallOp::create(
      rewriter, location, TypeRange{i32},
      SymbolRefAttr::get(
          rewriter.getContext(),
          continuous ? "obelisk_rt_v1_native_state_store_continuous_plane"
                     : "obelisk_rt_v1_native_state_store_plane"),
      ValueRange{context, base,
                 llvmConstant(rewriter, location, i64, stateBitCount), handle,
                 llvmConstant(rewriter, location, i64, width),
                 llvmConstant(rewriter, location, i32,
                              globalName == "__obelisk_state_unknown" ? 1 : 0),
                 input, changed});
}

// Copy a canonical byte buffer directly into a certified state plane. Only
// the final partial byte needs a read/modify/write; the rest remains memcpy.
// Continuous roots also retain their contribution for later force/release.
bool tryStoreBulkDirect(ConversionPatternRewriter &rewriter, Location location,
                        Value handle, Value input, uint64_t width,
                        StringRef globalName, const NativeStateLayout *layout,
                        bool assumeClean, bool continuous) {
  auto range = resolveDirectStaticStateRange(handle, width, layout);
  if (!range || range->offset % 8 || (range->guarded && !assumeClean) ||
      (continuous && range->guarded && !layout->directContinuous))
    return false;
  Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
  auto copy = [&](StringRef name, bool mask = false) {
    Value base = LLVM::AddressOfOp::create(rewriter, location, pointer, name);
    if (name.starts_with("__obelisk_continuous_"))
      base = LLVM::LoadOp::create(rewriter, location, pointer, base, 8);
    Value destination = byteGEP(rewriter, location, base, range->offset / 8);
    Value bytes =
        llvmConstant(rewriter, location, rewriter.getI64Type(), width / 8);
    Value ones = llvmConstant(rewriter, location, rewriter.getI8Type(), 255);
    if (mask)
      LLVM::MemsetOp::create(rewriter, location, destination, ones, bytes,
                             false);
    else
      LLVM::MemcpyOp::create(rewriter, location, destination, input, bytes,
                             false);
    if (unsigned tail = width % 8) {
      Value address = byteGEP(rewriter, location, destination, width / 8);
      Value old = LLVM::LoadOp::create(rewriter, location, rewriter.getI8Type(),
                                       address, 1);
      Value value = mask ? ones
                         : LLVM::LoadOp::create(
                               rewriter, location, rewriter.getI8Type(),
                               byteGEP(rewriter, location, input, width / 8), 1)
                               .getResult();
      Value bits = llvmConstant(rewriter, location, rewriter.getI8Type(),
                                (1u << tail) - 1);
      Value delta = arith::XOrIOp::create(rewriter, location, old, value);
      delta = arith::AndIOp::create(rewriter, location, delta, bits);
      Value merged = arith::XOrIOp::create(rewriter, location, old, delta);
      LLVM::StoreOp::create(rewriter, location, merged, address, 1);
    }
  };
  if (continuous && layout->directContinuous) {
    copy(globalName == "__obelisk_state_unknown"
             ? "__obelisk_continuous_unknown"
             : "__obelisk_continuous_value");
    if (globalName == "__obelisk_state_value")
      copy("__obelisk_continuous_mask", true);
  }
  copy(globalName);
  return true;
}

void notifyBulkSignal(ConversionPatternRewriter &rewriter, Location location,
                      Value handle, uint64_t width, Value oldValue,
                      Value oldUnknown, Value newValue, Value newUnknown,
                      schedule::SourceOwnerAttr sourceOwner = {}) {
  Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
  auto pointerOrNull = [&](Value value) {
    return value
               ? value
               : LLVM::ZeroOp::create(rewriter, location, pointer).getResult();
  };
  auto call = LLVM::CallOp::create(
      rewriter, location, TypeRange{},
      SymbolRefAttr::get(rewriter.getContext(),
                         "obelisk_rt_v1_scheduler_signal_transition"),
      ValueRange{loadCurrentRuntimeContext(rewriter, location), handle,
                 llvmConstant(rewriter, location, rewriter.getI64Type(), width),
                 oldValue, pointerOrNull(oldUnknown), newValue,
                 pointerOrNull(newUnknown)});
  if (sourceOwner)
    ::obelisk::schedule::set<schedule::metadata::evalSourceOwner>(call,
                                                                  sourceOwner);
}

class BulkRefCopyConversion final
    : public OpConversionPattern<sim::SimRefCopyOp> {
public:
  BulkRefCopyConversion(const TypeConverter &converter, MLIRContext *context,
                        uint64_t stateBitCount,
                        const NativeStateLayout *directLayout,
                        bool experimentalTwoState)
      : OpConversionPattern(converter, context, PatternBenefit(2)),
        stateBitCount(stateBitCount), directLayout(directLayout),
        experimentalTwoState(experimentalTwoState) {}

  LogicalResult
  matchAndRewrite(sim::SimRefCopyOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type valueType =
        cast<sim::RefType>(op.getSource().getType()).getElementType();
    std::optional<unsigned> width = nativeStateWidth(valueType);
    if (!width || *width <= 64 || adaptor.getSource().size() != 1 ||
        adaptor.getDestination().size() != 1)
      return failure();

    Value source = adaptor.getSource().front();
    Value destination = adaptor.getDestination().front();

    Location location = op.getLoc();
    bool twoState = useTwoStateSpecialization(op, experimentalTwoState);
    bool fourState = containsLogic(valueType) && !twoState;
    sim::SimFuncOp function = op->getParentOfType<sim::SimFuncOp>();
    sim::EntryKind entryKind = function.getEntryKind();
    bool continuous = op->hasAttr(continuousStoreAttrName) ||
                      entryKind == sim::EntryKind::Continuous ||
                      entryKind == sim::EntryKind::PortInput ||
                      entryKind == sim::EntryKind::PortOutput;
    bool sourceAssumeClean = native::has<native::BulkCopySourceAssumeClean>(op);

    Value sourceValue = loadBulkStatePlane(
        rewriter, location, source, *width, "__obelisk_state_value", false,
        stateBitCount, directLayout, sourceAssumeClean);
    Value sourceUnknown;
    if (fourState)
      sourceUnknown = loadBulkStatePlane(
          rewriter, location, source, *width, "__obelisk_state_unknown", true,
          stateBitCount, directLayout, sourceAssumeClean);
    Value oldValue = loadBulkStatePlane(rewriter, location, destination, *width,
                                        "__obelisk_state_value", false,
                                        stateBitCount, directLayout, false);
    Value oldUnknown;
    if (fourState)
      oldUnknown = loadBulkStatePlane(rewriter, location, destination, *width,
                                      "__obelisk_state_unknown", true,
                                      stateBitCount, directLayout, false);

    Value changed = entryAlloca(rewriter, location, rewriter.getI8Type(), 1, 1);
    storeBulkStatePlane(rewriter, location, destination, sourceValue, *width,
                        "__obelisk_state_value", stateBitCount, changed,
                        continuous);
    if (fourState)
      storeBulkStatePlane(rewriter, location, destination, sourceUnknown,
                          *width, "__obelisk_state_unknown", stateBitCount,
                          changed, continuous);

    // A generic packed store may be partially masked by a force or procedural
    // assign. Reload the visible destination planes before publication, just
    // like the scalar lowering required by IEEE 1800-2017 10.6.1-10.6.2.
    Value newValue = loadBulkStatePlane(rewriter, location, destination, *width,
                                        "__obelisk_state_value", false,
                                        stateBitCount, directLayout, false);
    Value newUnknown;
    if (fourState)
      newUnknown = loadBulkStatePlane(rewriter, location, destination, *width,
                                      "__obelisk_state_unknown", true,
                                      stateBitCount, directLayout, false);
    notifyBulkSignal(rewriter, location, destination, *width, oldValue,
                     oldUnknown, newValue, newUnknown);
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount;
  const NativeStateLayout *directLayout;
  bool experimentalTwoState;
};

class KnownValueConversion final
    : public OpConversionPattern<schedule::NativeKnownValueOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(schedule::NativeKnownValueOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto planes = adaptor.getInput();
    if (planes.size() == 1) {
      rewriter.replaceOpWithNewOp<arith::ConstantOp>(
          op, rewriter.getI1Type(), rewriter.getBoolAttr(true));
      return success();
    }
    if (planes.size() != 2 || !isa<IntegerType>(planes[1].getType()))
      return failure();
    Value zero = arith::ConstantOp::create(
        rewriter, op.getLoc(), planes[1].getType(),
        rewriter.getIntegerAttr(planes[1].getType(), 0));
    rewriter.replaceOpWithNewOp<arith::CmpIOp>(op, arith::CmpIPredicate::eq,
                                               planes[1], zero);
    return success();
  }
};

class RefLoadConversion final : public OpConversionPattern<sim::SimRefLoadOp> {
public:
  RefLoadConversion(
      const TypeConverter &converter, MLIRContext *context,
      uint64_t stateBitCount, const NativeStateLayout *directLayout,
      bool experimentalTwoState,
      const DenseMap<Operation *, NativeStateAccessInfo> &stateAccesses,
      const LocalStorageLowering *locals)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount),
        directLayout(directLayout), experimentalTwoState(experimentalTwoState),
        stateAccesses(stateAccesses), locals(locals) {}

  LogicalResult
  matchAndRewrite(sim::SimRefLoadOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = op.getResult().getType();
    bool twoState = readsKnownLocation(op, experimentalTwoState);
    auto proof = stateAccesses.find(op);
    const NativeStateAccessInfo *access =
        proof == stateAccesses.end() ? nullptr : &proof->second;
    std::optional<unsigned> width = nativeStateWidth(resultType);
    if (!width || adaptor.getReference().size() != 1)
      return failure();
    IntegerType plane = rewriter.getIntegerType(*width);
    bool assumeClean =
        ::obelisk::schedule::has<assumeCleanSpecializationAttr>(op);
    if (locals && locals->isMemoryValue(op.getResult())) {
      auto range = resolveDirectStaticStateRange(adaptor.getReference().front(),
                                                 *width, directLayout);
      auto read = [&](StringRef name, bool unknown) {
        if (locals->canBorrowMemory(op.getResult()) && range &&
            range->offset % 8 == 0 &&
            (!range->guarded || assumeClean)) {
          Value base = LLVM::AddressOfOp::create(
              rewriter, op.getLoc(),
              LLVM::LLVMPointerType::get(op.getContext()), name);
          return byteGEP(rewriter, op.getLoc(), base, range->offset / 8);
        }
        return loadBulkStatePlane(
            rewriter, op.getLoc(), adaptor.getReference().front(), *width, name,
            unknown, stateBitCount, directLayout, assumeClean);
      };
      SmallVector<Value> buffers{read("__obelisk_state_value", false)};
      if (containsLogic(resultType)) {
        if (twoState) {
          Value buffer = allocateBulkPlane(rewriter, op.getLoc(), *width);
          LLVM::MemsetOp::create(
              rewriter, op.getLoc(), buffer,
              llvmConstant(rewriter, op.getLoc(), rewriter.getI8Type(), 0),
              llvmConstant(rewriter, op.getLoc(), rewriter.getI64Type(),
                           (*width + 7) / 8),
              false);
          buffers.push_back(buffer);
        } else {
          buffers.push_back(read("__obelisk_state_unknown", true));
        }
      }
      rewriter.replaceOpWithMultiple(op, {ValueRange(buffers)});
      return success();
    }
    Value guardedPermission;
    if (auto range = resolveDirectStaticStateRange(
            adaptor.getReference().front(), *width, directLayout);
        range && range->guarded && !assumeClean)
      guardedPermission = staticSpecializationGuard(
          rewriter, op.getLoc(), range->staticID, OBELISK_RT_STATIC_ROOT_READ);
    Value value =
        loadStatePlane(rewriter, op.getLoc(), adaptor.getReference().front(),
                       plane, "__obelisk_state_value", false, stateBitCount,
                       directLayout, guardedPermission, assumeClean, access);
    value = maskStoragePlane(rewriter, op.getLoc(), value, resultType, false);
    if (isa<FloatType>(resultType))
      value =
          arith::BitcastOp::create(rewriter, op.getLoc(), resultType, value);
    SmallVector<Value> converted{value};
    if (containsLogic(resultType)) {
      Value unknown =
          twoState ? llvmConstant(rewriter, op.getLoc(), plane, 0)
                   : loadStatePlane(
                         rewriter, op.getLoc(), adaptor.getReference().front(),
                         plane, "__obelisk_state_unknown", true, stateBitCount,
                         directLayout, guardedPermission, assumeClean, access);
      converted.push_back(
          maskStoragePlane(rewriter, op.getLoc(), unknown, resultType, true));
    }
    SmallVector<ValueRange> replacements{ValueRange(converted)};
    rewriter.replaceOpWithMultiple(op, replacements);
    return success();
  }

private:
  uint64_t stateBitCount;
  const NativeStateLayout *directLayout;
  bool experimentalTwoState;
  const DenseMap<Operation *, NativeStateAccessInfo> &stateAccesses;
  const LocalStorageLowering *locals;
};

class RefStoreConversion final
    : public OpConversionPattern<sim::SimRefStoreOp> {
public:
  RefStoreConversion(
      const TypeConverter &converter, MLIRContext *context,
      uint64_t stateBitCount, const NativeStateLayout *directLayout,
      bool experimentalTwoState,
      const DenseMap<Operation *, NativeStateAccessInfo> &stateAccesses,
      const LocalStorageLowering *)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount),
        directLayout(directLayout), experimentalTwoState(experimentalTwoState),
        stateAccesses(stateAccesses) {
    setHasBoundedRewriteRecursion();
  }

  LogicalResult
  matchAndRewrite(sim::SimRefStoreOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getReference().size() != 1 || adaptor.getValue().empty())
      return failure();
    Type valueType = op.getValue().getType();
    bool twoState = useTwoStateSpecialization(op, experimentalTwoState);
    auto proof = stateAccesses.find(op);
    const NativeStateAccessInfo *access =
        proof == stateAccesses.end() ? nullptr : &proof->second;
    std::optional<unsigned> width = nativeStateWidth(valueType);
    if (!width)
      return failure();
    IntegerType plane = rewriter.getIntegerType(*width);
    bool assumeClean =
        ::obelisk::schedule::has<assumeCleanSpecializationAttr>(op);
    sim::SimFuncOp function = op->getParentOfType<sim::SimFuncOp>();
    sim::EntryKind entryKind = function.getEntryKind();
    bool runtimePublication =
        native::has<native::RuntimePublicationCertified>(function);
    bool continuous = !isa<sim::EventType>(valueType) &&
                      (op->hasAttr(continuousStoreAttrName) ||
                       entryKind == sim::EntryKind::Continuous ||
                       entryKind == sim::EntryKind::PortInput ||
                       entryKind == sim::EntryKind::PortOutput);
    std::optional<DirectStaticStateRange> directRange =
        resolveDirectStaticStateRange(adaptor.getReference().front(), *width,
                                      directLayout);
    // Guard the entire procedural store, including its publication. The clean
    // path can use exact static fanout without canonical reloads; a live writer
    // or observer invalidates the global flag before entering the slow path.
    // Continuous assignments must retain their contribution for a later
    // force/release (IEEE 1800-2023 10.6.2), even when currently unforced.
    bool directContinuous =
        continuous && directLayout && directLayout->directContinuous;
    if (directRange && directRange->guarded && !assumeClean &&
        (!continuous || directContinuous) && !runtimePublication &&
        !native::has<native::GuardedRefStore>(op)) {
      Block *head = rewriter.getInsertionBlock();
      Block *tail = rewriter.splitBlock(head, op->getIterator());
      Region *region = head->getParent();
      Block *fast = rewriter.createBlock(region, tail->getIterator());
      Block *slow = rewriter.createBlock(region, tail->getIterator());
      recordStaticSpecializationCFGBlocks(rewriter, head, 3);
      for (auto [block, clean] :
           {std::pair{fast, true}, std::pair{slow, false}}) {
        rewriter.setInsertionPointToEnd(block);
        Operation *clone = rewriter.clone(*op);
        native::mark<native::GuardedRefStore>(clone);
        if (clean)
          ::obelisk::schedule::set<assumeCleanSpecializationAttr>(
              clone, rewriter.getUnitAttr());
        cf::BranchOp::create(rewriter, op.getLoc(), tail);
      }
      rewriter.setInsertionPointToEnd(head);
      Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
      Value address =
          LLVM::AddressOfOp::create(rewriter, op.getLoc(), pointer,
                                    "__obelisk_state_specialization_fast_v1");
      Value flag = LLVM::LoadOp::create(rewriter, op.getLoc(),
                                        rewriter.getI32Type(), address, 4);
      Value allowed = arith::CmpIOp::create(
          rewriter, op.getLoc(), arith::CmpIPredicate::ne, flag,
          llvmConstant(rewriter, op.getLoc(), rewriter.getI32Type(), 0));
      markLikelyTrue(
          cf::CondBranchOp::create(rewriter, op.getLoc(), allowed, fast, slow));
      rewriter.eraseOp(op);
      return success();
    }
    bool needsNotification = true;
    // Exact fanout proves that an absent root has no language-level waiter.
    // Direct roots are also immune to external writes (VPI-off/read), while a
    // guarded VPI-full root may elide observers only in its clean fast body.
    if (directLayout && directLayout->transitionHandlesExact)
      if (directRange && (assumeClean || !directRange->guarded))
        needsNotification =
            directLayout->transitionHandles.contains(directRange->staticID);
    if (needsNotification && directRange &&
        (assumeClean || !directRange->guarded))
      if (auto roots = native::get<native::NoTransitionRoots>(function))
        needsNotification = !llvm::is_contained(
            roots.asArrayRef(), static_cast<int32_t>(directRange->staticID));
    if (isa<LLVM::LLVMPointerType>(adaptor.getValue().front().getType())) {
      Location location = op.getLoc();
      Value handle = adaptor.getReference().front();
      bool fourState = containsLogic(valueType) && !twoState;
      auto snapshot = [&](StringRef plane, bool unknown) {
        return loadBulkStatePlane(rewriter, location, handle, *width, plane,
                                  unknown, stateBitCount, directLayout,
                                  assumeClean);
      };
      Value oldValue, oldUnknown;
      if (needsNotification) {
        oldValue = snapshot("__obelisk_state_value", false);
        if (fourState)
          oldUnknown = snapshot("__obelisk_state_unknown", true);
      }
      auto store = [&](Value input, StringRef plane) {
        if (tryStoreBulkDirect(rewriter, location, handle, input, *width, plane,
                               directLayout, assumeClean, continuous))
          return true;
        Value changed =
            entryAlloca(rewriter, location, rewriter.getI8Type(), 1, 1);
        storeBulkStatePlane(rewriter, location, handle, input, *width, plane,
                            stateBitCount, changed, continuous);
        return false;
      };
      Value newValue = adaptor.getValue().front();
      bool direct = store(newValue, "__obelisk_state_value");
      Value newUnknown;
      if (fourState) {
        newUnknown = adaptor.getValue()[1];
        direct &= store(newUnknown, "__obelisk_state_unknown");
      }
      if (needsNotification) {
        // Runtime stores may be partially suppressed by an override. Publish
        // the actual visible value, with the same semantics as scalar stores.
        if (!direct) {
          newValue = snapshot("__obelisk_state_value", false);
          if (fourState)
            newUnknown = snapshot("__obelisk_state_unknown", true);
        }
        notifyBulkSignal(
            rewriter, location, handle, *width, oldValue, oldUnknown, newValue,
            newUnknown,
            ::obelisk::schedule::get<schedule::metadata::evalSourceOwner>(op));
      }
      rewriter.eraseOp(op);
      return success();
    }
    Value guardedPermission;
    if (directRange && directRange->guarded && !assumeClean)
      guardedPermission = staticSpecializationGuard(
          rewriter, op.getLoc(), directRange->staticID,
          OBELISK_RT_STATIC_ROOT_READ | OBELISK_RT_STATIC_ROOT_WRITE);
    Value storedValue = adaptor.getValue().front();
    if (isa<FloatType>(valueType))
      storedValue =
          arith::BitcastOp::create(rewriter, op.getLoc(), plane, storedValue);
    if (!directRange && !runtimePublication &&
        (isa_and_nonnull<IntegerType>(sim::getPackedScalarType(valueType)) ||
         containsLogic(valueType)) &&
        emitDirectDynamicPackedStore(
            rewriter, op.getLoc(), adaptor.getReference().front(), storedValue,
            adaptor.getValue().size() == 2 ? adaptor.getValue()[1] : Value{},
            directLayout, assumeClean, continuous, twoState,
            ::obelisk::schedule::get<schedule::metadata::evalSourceOwner>(op),
            access)) {
      rewriter.eraseOp(op);
      return success();
    }
    if (!needsNotification) {
      (void)storeStatePlane(
          rewriter, op.getLoc(), adaptor.getReference().front(), storedValue,
          "__obelisk_state_value", stateBitCount, directLayout,
          guardedPermission, assumeClean, /*trackChange=*/false, continuous);
      if (containsLogic(valueType) && !twoState)
        (void)storeStatePlane(
            rewriter, op.getLoc(), adaptor.getReference().front(),
            adaptor.getValue().size() == 2
                ? adaptor.getValue()[1]
                : llvmConstant(rewriter, op.getLoc(), plane, 0),
            "__obelisk_state_unknown", stateBitCount, directLayout,
            guardedPermission, assumeClean,
            /*trackChange=*/false, continuous);
      rewriter.eraseOp(op);
      return success();
    }
    Value oldValue =
        loadStatePlane(rewriter, op.getLoc(), adaptor.getReference().front(),
                       plane, "__obelisk_state_value", false, stateBitCount,
                       directLayout, guardedPermission, assumeClean, access);
    Value oldUnknown;
    if (containsLogic(valueType))
      oldUnknown = twoState
                       ? llvmConstant(rewriter, op.getLoc(), plane, 0)
                       : loadStatePlane(rewriter, op.getLoc(),
                                        adaptor.getReference().front(), plane,
                                        "__obelisk_state_unknown", true,
                                        stateBitCount, directLayout,
                                        guardedPermission, assumeClean, access);
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
    if (containsLogic(valueType) && !twoState) {
      Value storedUnknown = adaptor.getValue().size() == 2
                                ? adaptor.getValue()[1]
                                : llvmConstant(rewriter, op.getLoc(), plane, 0);
      Value unknownChanged = storeStatePlane(
          rewriter, op.getLoc(), adaptor.getReference().front(), storedUnknown,
          "__obelisk_state_unknown", stateBitCount, directLayout,
          guardedPermission, assumeClean,
          /*trackChange=*/true, continuous);
      notificationUnknown = arith::SelectOp::create(
          rewriter, op.getLoc(), unknownChanged, storedUnknown, oldUnknown);
    }
    // A generic packed store can update only the currently unmasked bits.
    // Reload its canonical result so partial external forces cannot leak the
    // attempted value through transition publication. Direct clean stores
    // have no override mask and keep the select-only fast path above.
    bool needsVisibleReload =
        sim::getPackedWidth(valueType).has_value() &&
        ((continuous &&
          !(directContinuous &&
            (assumeClean || (directRange && !directRange->guarded)))) ||
         !directLayout || !directRange ||
         (directRange->guarded && !assumeClean));
    if (needsVisibleReload) {
      notificationValue =
          loadStatePlane(rewriter, op.getLoc(), adaptor.getReference().front(),
                         plane, "__obelisk_state_value", false, stateBitCount,
                         directLayout, guardedPermission, assumeClean, access);
      if (containsLogic(valueType))
        notificationUnknown =
            twoState ? llvmConstant(rewriter, op.getLoc(), plane, 0)
                     : loadStatePlane(rewriter, op.getLoc(),
                                      adaptor.getReference().front(), plane,
                                      "__obelisk_state_unknown", true,
                                      stateBitCount, directLayout,
                                      guardedPermission, assumeClean, access);
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
      // IEEE 1800-2017 Clause 31.9.1 transport commits must wake the
      // runtime-owned delayed-terminal coordinator. Select the generic
      // publication call while lowering this structurally certified cold
      // function; ordinary stores retain the branch-free static AOT call.
      notifySignal(
          rewriter, op.getLoc(), adaptor.getReference().front(), *width,
          oldValue, oldUnknown, notificationValue, notificationUnknown,
          !runtimePublication && directRange &&
                  (assumeClean || !directRange->guarded) && directLayout &&
                  directLayout->transitionHandlesExact
              ? directRange
              : std::nullopt,
          ::obelisk::schedule::get<schedule::metadata::evalSourceOwner>(op));
    }
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount;
  const NativeStateLayout *directLayout;
  bool experimentalTwoState;
  const DenseMap<Operation *, NativeStateAccessInfo> &stateAccesses;
};

class NetReadConversion final : public OpConversionPattern<sim::SimNetReadOp> {
public:
  NetReadConversion(const TypeConverter &converter, MLIRContext *context,
                    uint64_t stateBitCount,
                    const NativeStateLayout *directLayout,
                    bool experimentalTwoState)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount),
        directLayout(directLayout), experimentalTwoState(experimentalTwoState) {
  }

  LogicalResult
  matchAndRewrite(sim::SimNetReadOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = op.getResult().getType();
    bool twoState = readsKnownLocation(op, experimentalTwoState);
    std::optional<unsigned> width = nativeStateWidth(resultType);
    if (!width || adaptor.getNet().size() != 1)
      return failure();
    IntegerType plane = rewriter.getIntegerType(*width);
    Value value = loadStatePlane(
        rewriter, op.getLoc(), adaptor.getNet().front(), plane,
        "__obelisk_state_value", false, stateBitCount, directLayout);
    value = maskStoragePlane(rewriter, op.getLoc(), value, resultType, false);
    if (isa<FloatType>(resultType))
      value =
          arith::BitcastOp::create(rewriter, op.getLoc(), resultType, value);
    SmallVector<Value> converted{value};
    if (containsLogic(resultType)) {
      Value unknown = twoState ? llvmConstant(rewriter, op.getLoc(), plane, 0)
                               : loadStatePlane(rewriter, op.getLoc(),
                                                adaptor.getNet().front(), plane,
                                                "__obelisk_state_unknown", true,
                                                stateBitCount, directLayout);
      converted.push_back(
          maskStoragePlane(rewriter, op.getLoc(), unknown, resultType, true));
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

class DriverReadConversion final
    : public OpConversionPattern<sim::SimDriverReadOp> {
public:
  DriverReadConversion(const TypeConverter &converter, MLIRContext *context,
                       uint64_t stateBitCount,
                       const NativeStateLayout *directLayout,
                       bool experimentalTwoState)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount),
        directLayout(directLayout), experimentalTwoState(experimentalTwoState) {
  }

  LogicalResult
  matchAndRewrite(sim::SimDriverReadOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resultType = op.getResult().getType();
    bool twoState = useTwoStateSpecialization(op, experimentalTwoState);
    std::optional<unsigned> width = nativeStateWidth(resultType);
    if (!width || adaptor.getDriver().size() != 1)
      return failure();
    IntegerType plane = rewriter.getIntegerType(*width);
    Value value = loadStatePlane(
        rewriter, op.getLoc(), adaptor.getDriver().front(), plane,
        "__obelisk_state_value", false, stateBitCount, directLayout);
    value = maskStoragePlane(rewriter, op.getLoc(), value, resultType, false);
    if (isa<FloatType>(resultType))
      value =
          arith::BitcastOp::create(rewriter, op.getLoc(), resultType, value);
    SmallVector<Value> converted{value};
    if (containsLogic(resultType)) {
      Value unknown = twoState
                          ? llvmConstant(rewriter, op.getLoc(), plane, 0)
                          : loadStatePlane(rewriter, op.getLoc(),
                                           adaptor.getDriver().front(), plane,
                                           "__obelisk_state_unknown", true,
                                           stateBitCount, directLayout);
      converted.push_back(
          maskStoragePlane(rewriter, op.getLoc(), unknown, resultType, true));
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

class NetWriteConversion final
    : public OpConversionPattern<sim::SimNetWriteOp> {
public:
  NetWriteConversion(const TypeConverter &converter, MLIRContext *context,
                     uint64_t stateBitCount,
                     const NativeStateLayout *directLayout,
                     bool experimentalTwoState)
      : OpConversionPattern(converter, context), stateBitCount(stateBitCount),
        directLayout(directLayout), experimentalTwoState(experimentalTwoState) {
  }

  LogicalResult
  matchAndRewrite(sim::SimNetWriteOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type valueType = op.getValue().getType();
    bool twoState = useTwoStateSpecialization(op, experimentalTwoState);
    std::optional<unsigned> width = nativeStateWidth(valueType);
    if (!width || adaptor.getNet().size() != 1 || adaptor.getValue().empty())
      return failure();
    IntegerType plane = rewriter.getIntegerType(*width);
    Value handle = adaptor.getNet().front();
    Value oldValue = loadStatePlane(rewriter, op.getLoc(), handle, plane,
                                    "__obelisk_state_value", false,
                                    stateBitCount, directLayout);
    Value oldUnknown;
    if (containsLogic(valueType))
      oldUnknown = twoState ? llvmConstant(rewriter, op.getLoc(), plane, 0)
                            : loadStatePlane(rewriter, op.getLoc(), handle,
                                             plane, "__obelisk_state_unknown",
                                             true, stateBitCount, directLayout);
    Value newValue = adaptor.getValue().front();
    if (isa<FloatType>(valueType))
      newValue =
          arith::BitcastOp::create(rewriter, op.getLoc(), plane, newValue);
    Value newUnknown = adaptor.getValue().size() == 2 && !twoState
                           ? adaptor.getValue()[1]
                           : Value{};
    (void)storeStatePlane(rewriter, op.getLoc(), handle, newValue,
                          "__obelisk_state_value", stateBitCount, directLayout);
    if (containsLogic(valueType) && !twoState)
      (void)storeStatePlane(rewriter, op.getLoc(), handle, newUnknown,
                            "__obelisk_state_unknown", stateBitCount,
                            directLayout);

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
          ValueRange{runtimeContext, handle,
                     llvmConstant(rewriter, op.getLoc(), rewriter.getI32Type(),
                                  *width),
                     save(oldValue), save(newValue)});
    } else {
      notifySignal(
          rewriter, op.getLoc(), handle, *width, oldValue, oldUnknown, newValue,
          containsLogic(valueType) ? newUnknown : Value{},
          resolveDirectStaticStateRange(handle, *width, directLayout),
          ::obelisk::schedule::get<schedule::metadata::evalSourceOwner>(op));
    }
    rewriter.eraseOp(op);
    return success();
  }

private:
  uint64_t stateBitCount;
  const NativeStateLayout *directLayout;
  bool experimentalTwoState;
};

class NetCountDriversConversion final
    : public OpConversionPattern<sim::SimNetCountDriversOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimNetCountDriversOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getNet().size() != 1)
      return failure();
    Location location = op.getLoc();
    Type i32 = rewriter.getI32Type();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    SmallVector<Value, 5> outputs;
    outputs.reserve(5);
    for (unsigned index = 0; index != 5; ++index) {
      Value output = entryAlloca(rewriter, location, i32, 1, 4);
      LLVM::StoreOp::create(rewriter, location,
                            llvmConstant(rewriter, location, i32, 0), output,
                            4);
      outputs.push_back(output);
    }
    SmallVector<Value, 7> arguments{context, adaptor.getNet().front()};
    llvm::append_range(arguments, outputs);
    Value status = LLVM::CallOp::create(
                       rewriter, location, TypeRange{i32},
                       SymbolRefAttr::get(rewriter.getContext(),
                                          "obelisk_rt_v1_net_count_drivers"),
                       arguments)
                       .getResult();
    reportRuntimeControlStatus(rewriter, location, context, status);
    SmallVector<Value, 5> replacements;
    for (Value output : outputs)
      replacements.push_back(
          LLVM::LoadOp::create(rewriter, location, i32, output, 4));
    rewriter.replaceOp(op, replacements);
    return success();
  }
};

class PassSwitchControlConversion final
    : public OpConversionPattern<sim::SimPassSwitchControlOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimPassSwitchControlOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getControl().empty() || adaptor.getControl().size() > 2 ||
        op.getPassSwitchId() >= UINT32_MAX)
      return failure();
    Location location = op.getLoc();
    Type i32 = rewriter.getI32Type();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    auto extend = [&](Value value) -> Value {
      return LLVM::ZExtOp::create(rewriter, location, i32, value);
    };
    Value unknown = adaptor.getControl().size() == 2
                        ? extend(adaptor.getControl()[1])
                        : llvmConstant(rewriter, location, i32, 0);
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_pass_switch_control"),
            ValueRange{
                context,
                llvmConstant(rewriter, location, i32, op.getPassSwitchId()),
                extend(adaptor.getControl().front()), unknown})
            .getResult();
    reportRuntimeControlStatus(rewriter, location, context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class PassSwitchControlDelayedConversion final
    : public OpConversionPattern<sim::SimPassSwitchControlDelayedOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimPassSwitchControlDelayedOp op,
                  OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getControl().empty() || adaptor.getTurnOnDelay().size() != 1 ||
        adaptor.getTurnOffDelay().size() != 1 ||
        adaptor.getUnknownDelay().size() != 1 ||
        op.getPassSwitchId() >= UINT32_MAX)
      return failure();
    Location location = op.getLoc();
    Type pointer = LLVM::LLVMPointerType::get(rewriter.getContext());
    Type i32 = rewriter.getI32Type();
    Value contextAddress = LLVM::AddressOfOp::create(
        rewriter, location, pointer, "__obelisk_current_context");
    Value context =
        LLVM::LoadOp::create(rewriter, location, pointer, contextAddress, 8);
    auto extend = [&](Value value) -> Value {
      return LLVM::ZExtOp::create(rewriter, location, i32, value);
    };
    Value unknown = adaptor.getControl().size() == 2
                        ? extend(adaptor.getControl()[1])
                        : llvmConstant(rewriter, location, i32, 0);
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_pass_switch_control_delayed"),
            ValueRange{
                context,
                llvmConstant(rewriter, location, i32, op.getPassSwitchId()),
                extend(adaptor.getControl().front()), unknown,
                adaptor.getTurnOnDelay().front(),
                adaptor.getTurnOffDelay().front(),
                adaptor.getUnknownDelay().front()})
            .getResult();
    reportRuntimeControlStatus(rewriter, location, context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

class MosDriveDelayedConversion final
    : public OpConversionPattern<sim::SimMosDriveDelayedOp> {
public:
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(sim::SimMosDriveDelayedOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    if (adaptor.getControl().empty() || adaptor.getRiseDelay().size() != 1 ||
        adaptor.getFallDelay().size() != 1 ||
        adaptor.getTurnoffDelay().size() != 1 ||
        op.getPassSwitchId() >= UINT32_MAX)
      return failure();
    Location location = op.getLoc();
    Type i32 = rewriter.getI32Type();
    Value context = loadCurrentRuntimeContext(rewriter, location);
    auto extend = [&](Value value) -> Value {
      return LLVM::ZExtOp::create(rewriter, location, i32, value);
    };
    Value unknown = adaptor.getControl().size() == 2
                        ? extend(adaptor.getControl()[1])
                        : llvmConstant(rewriter, location, i32, 0);
    Value status =
        LLVM::CallOp::create(
            rewriter, location, TypeRange{i32},
            SymbolRefAttr::get(rewriter.getContext(),
                               "obelisk_rt_v1_mos_drive_delayed"),
            ValueRange{
                context,
                llvmConstant(rewriter, location, i32, op.getPassSwitchId()),
                extend(adaptor.getControl().front()), unknown,
                adaptor.getRiseDelay().front(), adaptor.getFallDelay().front(),
                adaptor.getTurnoffDelay().front()})
            .getResult();
    reportRuntimeControlStatus(rewriter, location, context, status);
    rewriter.eraseOp(op);
    return success();
  }
};

} // namespace

void populateStateReadWriteToLLVMConversionPatterns(
    RewritePatternSet &patterns, TypeConverter &converter,
    uint64_t stateBitCount, const NativeStateLayout *directLayout,
    bool experimentalTwoState,
    const DenseMap<Operation *, NativeStateAccessInfo> &stateAccesses,
    const LocalStorageLowering *locals) {
  patterns.add<KnownValueConversion>(converter, patterns.getContext());
  patterns.add<BulkRefCopyConversion>(converter, patterns.getContext(),
                                      stateBitCount, directLayout,
                                      experimentalTwoState);
  patterns.add<RefLoadConversion, RefStoreConversion>(
      converter, patterns.getContext(), stateBitCount, directLayout,
      experimentalTwoState, stateAccesses, locals);
  patterns.add<NetReadConversion, DriverReadConversion, NetWriteConversion>(
      converter, patterns.getContext(), stateBitCount, directLayout,
      experimentalTwoState);
  patterns.add<NetCountDriversConversion>(converter, patterns.getContext());
  patterns.add<PassSwitchControlConversion, PassSwitchControlDelayedConversion,
               MosDriveDelayedConversion>(converter, patterns.getContext());
}

} // namespace obelisk::detail
