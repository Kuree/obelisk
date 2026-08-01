//===- SimulationPackedLowering.cpp - Packed simulation conversion ----===//

#include "SimulationPackedLowering.h"

#include "obelisk/Analysis/StateDomainAnalysis.h"
#include "obelisk/Conversion/RuntimeToLLVM.h"
#include "obelisk/Conversion/SimulationToRuntime.h"
#include "obelisk/Conversion/SimulationToStandard.h"
#include "obelisk/Dialect/Runtime/RuntimeOps.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;

namespace obelisk::detail {

namespace {

LogicalResult convertNativeAggregateType(Type type,
                                         SmallVectorImpl<Type> &results) {
  std::optional<unsigned> width = nativeStateWidth(type);
  if (!width)
    return failure();
  Type plane = IntegerType::get(type.getContext(), *width);
  results.push_back(plane);
  if (containsLogic(type))
    results.push_back(plane);
  return success();
}

bool hasNoLogic(Operation *operation) {
  for (Type type : operation->getOperandTypes())
    if (containsLogic(type))
      return false;
  for (Type type : operation->getResultTypes())
    if (containsLogic(type))
      return false;
  for (Region &region : operation->getRegions())
    for (Block &block : region)
      for (BlockArgument argument : block.getArguments())
        if (containsLogic(argument.getType()))
          return false;
  return true;
}

} // namespace

LogicalResult lowerPackedSimulationOperations(
    ModuleOp module, const llvm::DataLayout &dataLayout,
    const NativeStateLayout &stateLayout, bool enableDirectStaticState,
    const NativeStaticNBAPlan *staticNBAPlan, bool vpiAllowsWrite) {
  MLIRContext *context = module.getContext();
  // Consume the whole-design X/Z proof in the AOT path after suspension
  // threading has reached its final SSA shape. Signatures and canonical frames
  // remain two-plane ABI objects, but proven block arguments, call results,
  // and local producers expose a constant-zero unknown plane to LLVM.
  DenseSet<Value> nativeTwoStateValues;
  DenseSet<Operation *> nativeTwoStateOperations;
  WalkResult stateDomainsComputed = module.walk([&](sim::SimDesignOp design) {
    FailureOr<StateDomainAnalysis> stateDomains =
        StateDomainAnalysis::compute(design, /*proveInductiveRoots=*/false);
    if (failed(stateDomains))
      return WalkResult::interrupt();
    for (sim::SimFuncOp function :
         design.getBody().front().getOps<sim::SimFuncOp>()) {
      if (function.isExternal())
        continue;
      for (Block &block : function.getBody()) {
        for (BlockArgument argument : block.getArguments())
          if (isa<sim::LogicType>(argument.getType()) &&
              stateDomains->isTwoState(argument))
            nativeTwoStateValues.insert(argument);
        for (Operation &operation : block)
          for (Value result : operation.getResults())
            if (isa<sim::LogicType>(result.getType()) &&
                stateDomains->isTwoState(result))
              nativeTwoStateValues.insert(result);
      }
    }
    return WalkResult::advance();
  });
  if (stateDomainsComputed.wasInterrupted())
    return failure();
  for (Value value : nativeTwoStateValues) {
    auto result = dyn_cast<OpResult>(value);
    if (!result || result.getOwner()->getNumResults() != 1)
      continue;
    nativeTwoStateOperations.insert(result.getOwner());
  }

  // Record the net driven by each operation before dialect conversion starts
  // rewriting function signatures and their block arguments.  Conversion
  // patterns should inspect stable operation metadata instead of chasing the
  // source SSA graph while it is being replaced.
  annotateStaticDriverNets(module, stateLayout);

  SimulationToStandardTypeConverter packedConverter;
  addSimulationPackedAggregateTypeConversions(packedConverter);
  packedConverter.addConversion(
      [](sim::UnpackedArrayType type, SmallVectorImpl<Type> &results) {
        return convertNativeAggregateType(type, results);
      });
  packedConverter.addConversion(
      [](sim::UnpackedStructType type, SmallVectorImpl<Type> &results) {
        return convertNativeAggregateType(type, results);
      });
  packedConverter.addConversion(
      [](sim::UnpackedUnionType type, SmallVectorImpl<Type> &results) {
        return convertNativeAggregateType(type, results);
      });
  addSimulationToRuntimeTypeConversions(packedConverter);
  packedConverter.addConversion([context](Type type) -> std::optional<Type> {
    if (isa<sim::RefType, sim::NetType, sim::DriverType, sim::EventType,
            sim::ProcessType, sim::ControlType, sim::ObserverType,
            sim::CovergroupHandleType>(type))
      return IntegerType::get(context, 64);
    return std::nullopt;
  });
  packedConverter.addConversion([context](Type type) -> std::optional<Type> {
    if (sim::isManagedHandleType(type))
      return IntegerType::get(context, 64);
    return std::nullopt;
  });
  packedConverter.addConversion([context](sim::ArgumentRefType) -> Type {
    return IntegerType::get(context, 192);
  });
  packedConverter.addConversion(
      [context](sim::ManagedRefType, SmallVectorImpl<Type> &results) {
        results.push_back(IntegerType::get(context, 64));
        results.push_back(IntegerType::get(context, 64));
        return success();
      });
  ReferenceArgumentMap referenceArguments;
  WalkResult lifetimeInputs = module.walk([&](sim::SimFuncOp function) {
    if (function.getBody().empty())
      return WalkResult::advance();
    // Observer captures are borrowed from the persistent computed-wait
    // record. Unlike an ordinary direct call, invoking an observer does not
    // transfer one retained reference per argument, so its return must not
    // consume captured automatic state. The waiting activation owns that
    // state across suspension and releases it on resumption or cancellation.
    if (function.getEntryKind() == sim::EntryKind::Observer)
      return WalkResult::advance();
    unsigned physical = 0;
    for (BlockArgument argument : function.getBody().front().getArguments()) {
      SmallVector<Type> converted;
      if (failed(packedConverter.convertType(argument.getType(), converted)))
        return WalkResult::interrupt();
      if (isa<sim::RefType>(argument.getType())) {
        if (converted.size() != 1)
          return WalkResult::interrupt();
        referenceArguments[function.getOperation()].push_back(physical);
      }
      physical += converted.size();
    }
    return WalkResult::advance();
  });
  if (lifetimeInputs.wasInterrupted())
    return failure();
  // This is transaction-local metadata produced only by the AOT signature
  // pattern below. Never consume a same-named source attribute.
  module.walk([](sim::SimFuncOp function) {
    function->removeAttr(nativeTwoStateBlockUnknownsAttr);
  });
  RewritePatternSet packedPatterns(context);
  populateSimulationToStandardPatterns(packedConverter, packedPatterns,
                                       nativeTwoStateOperations);
  populateSimulationPackedAggregateViewPatterns(packedConverter,
                                                packedPatterns);
  populateSimulationToRuntimePatterns(packedConverter, packedPatterns);
  populateFunctionTypeConversionPatterns(packedPatterns, packedConverter,
                                         nativeTwoStateValues);
  populateAggregateToLLVMConversionPatterns(packedPatterns, packedConverter);
  populateControlToLLVMConversionPatterns(packedPatterns, packedConverter);
  populateEventToLLVMConversionPatterns(packedPatterns, packedConverter);
  populateSuspensionTypeConversionPatterns(packedPatterns, packedConverter);
  populateReferenceLifetimeToLLVMConversionPatterns(packedPatterns,
                                                    packedConverter);
  populateNativeHandleConversionPatterns(
      packedPatterns, packedConverter, stateLayout.storage, stateLayout.nets,
      stateLayout.drivers);
  populateSchedulerToLLVMConversionPatterns(packedPatterns, packedConverter);
  populateStateReadWriteToLLVMConversionPatterns(
      packedPatterns, packedConverter, stateLayout.bitCount,
      enableDirectStaticState ? &stateLayout : nullptr);
  populateOverrideToLLVMConversionPatterns(packedPatterns, packedConverter,
                                           stateLayout.bitCount);
  populateManagedToLLVMConversionPatterns(
      packedPatterns, packedConverter, dataLayout, stateLayout.bitCount);
  populateDriverToLLVMConversionPatterns(packedPatterns, packedConverter,
                                         stateLayout);
  populateNBAToLLVMConversionPatterns(
      packedPatterns, packedConverter, stateLayout.bitCount,
      staticNBAPlan, staticNBAPlan != nullptr, vpiAllowsWrite);
  ConversionTarget packedTarget(*context);
  packedTarget.addIllegalOp<
      sim::SimBytesConstantOp, sim::SimFinishOp, sim::SimStopOp,
      sim::SimFatalOp, sim::SimTerminationRequestedOp, sim::SimTimeNowOp,
      sim::SimDisplayOp, sim::SimFileOpenMCDOp, sim::SimFileOpenOp,
      sim::SimFileCloseOp, sim::SimFileFlushOp, sim::SimFileGetcOp,
      sim::SimFileUngetcOp, sim::SimFileGetlineOp, sim::SimFileReadPackedOp,
      sim::SimFileEofOp, sim::SimFileSeekOp, sim::SimFileTellOp,
      sim::SimFileRewindOp>();
  packedTarget.addIllegalOp<
      sim::SimContextStorageOp, sim::SimContextNetOp, sim::SimContextDriverOp,
      sim::SimContextEventOp, sim::SimRefAllocOp, sim::SimRefReleaseOwnerOp,
      sim::SimRefLoadOp, sim::SimRefStoreOp, sim::SimOverrideOp,
      sim::SimReleaseOverrideOp,
      sim::SimNetExtractOp, sim::SimRefExtractOp, sim::SimRefDynExtractOp,
      sim::SimRefSubelementOp, sim::SimRefArrayElementOp, sim::SimNetReadOp,
      sim::SimDriverDriveOp, sim::SimDriverExtractOp,
      sim::SimDriverDynExtractOp, sim::SimDriverSubelementOp,
      sim::SimDriverArrayElementOp, sim::SimNBAEnqueueOp,
      sim::SimEventTriggerOp, sim::SimEventTriggeredOp, sim::SimEventEqualOp,
      sim::SimDisableChildrenOp, sim::SimControlEnterOp, sim::SimControlLeaveOp,
      sim::SimControlDisableOp, sim::SimStaticOnceOp, sim::SimDeferredOnceOp,
      sim::SimMonitorRegisterOp, sim::SimMonitorControlOp,
      sim::SimMonitorCurrentOp, sim::SimBitsDynExtractOp, sim::SimClassNullOp,
      sim::SimCovergroupNullOp, sim::SimCovergroupCreateOp,
      sim::SimCovergroupSampleEnabledOp, sim::SimCovergroupBinHitOp,
      sim::SimCovergroupStartOp, sim::SimCovergroupStopOp,
      sim::SimCovergroupInstanceQueryOp, sim::SimCovergroupTypeQueryOp,
      sim::SimManagedNullOp, sim::SimManagedIsNullOp, sim::SimEventNullOp,
      sim::SimContainerSizeOp, sim::SimContainerCreateLikeOp,
      sim::SimContainerCreateOp, sim::SimContainerCloneOp,
      sim::SimContainerDeleteOp, sim::SimQueueDeleteOp, sim::SimQueueInsertOp,
      sim::SimContainerReadOp, sim::SimContainerWriteOp, sim::SimAssocCreateOp,
      sim::SimAssocReadOp, sim::SimAssocWriteOp, sim::SimAssocExistsOp,
      sim::SimAssocDeleteOp, sim::SimAssocSetDefaultOp, sim::SimAssocTraverseOp,
      sim::SimRandomNextOp, sim::SimRandomSeedOp, sim::SimRandomBoundedOp,
      sim::SimStringLiteralOp, sim::SimStringFromPackedOp,
      sim::SimStringToPackedOp, sim::SimStringConcatOp, sim::SimStringRepeatOp,
      sim::SimStringLengthOp, sim::SimStringGetcOp, sim::SimStringPutcOp,
      sim::SimStringSubstrOp, sim::SimStringCompareOp,
      sim::SimStringCaseConvertOp, sim::SimStringParseIntegerOp,
      sim::SimStringParseRealOp, sim::SimStringFormatIntegerOp,
      sim::SimStringFormatRealOp, sim::SimFileOpenStringMCDOp,
      sim::SimFileOpenStringOp, sim::SimFileGetlineStringOp,
      sim::SimClassAllocOp, sim::SimClassCopyOp, sim::SimClassIsInstanceOp,
      sim::SimClassIdOp, sim::SimClassCastOp, sim::SimClassFieldRefOp,
      sim::SimClassRootBindOp, sim::SimManagedLoadOp, sim::SimManagedStoreOp,
      sim::SimManagedNBAEnqueueOp, sim::SimReferencePathNBAEnqueueOp,
      sim::SimArgumentRefFromRefOp, sim::SimArgumentRefFromManagedOp,
      sim::SimReferencePathIndexOp, sim::SimReferencePathAssocOp,
      sim::SimArgumentRefFromPathOp, sim::SimArgumentRefLoadOp,
      sim::SimArgumentRefStoreOp, sim::SimClassDirectCallOp,
      sim::SimClassVirtualCallOp, sim::SimWeakCreateOp, sim::SimWeakGetOp,
      sim::SimWeakClearOp, sim::SimGCSafepointOp>();
  packedTarget
      .addIllegalOp<sim::SimAggregateDefaultOp, sim::SimAggregateConstructOp,
                    sim::SimAggregateExtractOp, sim::SimAggregateInsertOp,
                    sim::SimArrayDynExtractOp, sim::SimUnionConstructOp,
                    sim::SimUnionExtractOp, sim::SimUnionIsActiveOp>();
  packedTarget.addLegalDialect<runtime::ObeliskRuntimeDialect>();
  packedTarget.addLegalOp<sim::SimContextRuntimeOp, sim::SimStatusCheckOp>();
  packedTarget.addDynamicallyLegalOp<sim::SimFuncOp>(
      [&](sim::SimFuncOp function) {
        return packedConverter.isSignatureLegal(function.getFunctionType()) &&
               packedConverter.isLegal(&function.getBody());
      });
  packedTarget.addDynamicallyLegalOp<
      sim::SimCallOp, sim::SimDPICallOp, sim::SimSpawnOp, sim::SimReturnOp,
      sim::SimTaskCallOp, sim::SimObserverBindOp, sim::SimPackedFlattenOp,
      sim::SimPackedUnflattenOp, sim::SimSuspendDelayOp,
      sim::SimSuspendChangeOp, sim::SimSuspendEdgeOp, sim::SimSuspendEdgeIffOp,
      sim::SimSuspendLevelOp, sim::SimSuspendAnyOp, sim::SimSuspendEventOp,
      sim::SimSuspendForeverOp, sim::SimSuspendAwaitOp, sim::SimSuspendJoinOp,
      sim::SimSuspendChildrenOp, sim::SimSuspendObserveOp>(
      [&](Operation *operation) { return packedConverter.isLegal(operation); });
  packedTarget.addDynamicallyLegalDialect<
      sim::ObeliskSimulationDialect, arith::ArithDialect,
      cf::ControlFlowDialect, func::FuncDialect>([&](Operation *operation) {
    return hasNoLogic(operation) && packedConverter.isLegal(operation);
  });
  packedTarget.addDynamicallyLegalOp<cf::BranchOp, cf::CondBranchOp>(
      [&](Operation *operation) { return packedConverter.isLegal(operation); });
  packedTarget.addDynamicallyLegalOp<ModuleOp>(hasNoLogic);
  packedTarget.markUnknownOpDynamicallyLegal(hasNoLogic);
  if (failed(
          applyFullConversion(module, packedTarget, std::move(packedPatterns))))
    return failure();
  if (failed(materializeDPIThunks(module)))
    return failure();

  // Region signature conversion records the physical unknown-plane block
  // arguments that the whole-design proof made redundant. Replace them only
  // after dialect conversion has finished remapping every original logic use;
  // doing this inside the signature pattern would not update future one-to-N
  // operand adaptors owned by the conversion driver.
  WalkResult specializedBlockArguments =
      module.walk([&](sim::SimFuncOp function) {
        auto mappings = function->getAttrOfType<ArrayAttr>(
            nativeTwoStateBlockUnknownsAttr);
        if (!mappings)
          return WalkResult::advance();
        if (mappings.size() != function.getBody().getBlocks().size()) {
          function.emitOpError(
              "has invalid native two-state block-argument metadata");
          return WalkResult::interrupt();
        }
        OpBuilder builder(context);
        for (auto [block, mapping] :
             llvm::zip_equal(function.getBody(), mappings)) {
          auto indices = dyn_cast<DenseI64ArrayAttr>(mapping);
          if (!indices) {
            function.emitOpError(
                "has malformed native two-state block-argument metadata");
            return WalkResult::interrupt();
          }
          builder.setInsertionPointToStart(&block);
          for (int64_t index : indices.asArrayRef()) {
            if (index < 0 ||
                static_cast<uint64_t>(index) >= block.getNumArguments()) {
              function.emitOpError(
                  "has out-of-range native two-state block argument");
              return WalkResult::interrupt();
            }
            BlockArgument argument =
                block.getArgument(static_cast<unsigned>(index));
            auto type = dyn_cast<IntegerType>(argument.getType());
            if (!type) {
              function.emitOpError(
                  "has non-integer native two-state unknown plane");
              return WalkResult::interrupt();
            }
            Value zero = arith::ConstantOp::create(
                builder, function.getLoc(), type,
                builder.getIntegerAttr(type, APInt::getZero(type.getWidth())));
            argument.replaceAllUsesWith(zero);
          }
        }
        function->removeAttr(nativeTwoStateBlockUnknownsAttr);
        return WalkResult::advance();
      });
  if (specializedBlockArguments.wasInterrupted() ||
      failed(threadRuntimeStatuses(module)) ||
      failed(releaseNativeAutomaticState(module, referenceArguments)))
    return failure();
  if (failed(validateRuntimeToLLVMPreconditions(module, dataLayout)))
    return failure();
  return success();
}

} // namespace obelisk::detail
