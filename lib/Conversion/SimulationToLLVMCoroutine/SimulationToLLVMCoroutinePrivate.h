//===- SimulationToLLVMCoroutinePrivate.h - Shared lowering support ------===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_PRIVATE_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_PRIVATE_H

#include "PackedStorageLayout.h"
#include "SimulationProcessRuntimeABI.h"
#include "obelisk/Analysis/ClockInferenceAnalysis.h"
#include "obelisk/Dialect/Schedule/ScheduleAttrs.h"
#include "obelisk/Dialect/Schedule/ScheduleFieldEnums.h"

#include "obelisk/Analysis/NativeStateLayoutAnalysis.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace llvm {
class DataLayout;
}

namespace mlir {
class AnalysisManager;
class Pass;
class ConversionPatternRewriter;
class RewritePatternSet;
class TypeConverter;
namespace cf {
class CondBranchOp;
}
} // namespace mlir

namespace obelisk {
class SimulationProcessFrameAnalysis;
}

namespace obelisk::sim {
class AssocArrayType;
class SimFuncOp;
} // namespace obelisk::sim

namespace obelisk::detail {

/// Function declarations never contain other native function declarations.
/// Inventory their symbols without walking every operation in every body.
template <typename FunctionOp, typename Callback>
mlir::WalkResult walkNativeFunctions(mlir::Operation *root,
                                     Callback &&callback) {
  return root->walk<mlir::WalkOrder::PreOrder>([&](FunctionOp function) {
    if constexpr (std::is_same_v<std::invoke_result_t<Callback, FunctionOp>,
                                 mlir::WalkResult>) {
      if (callback(function).wasInterrupted())
        return mlir::WalkResult::interrupt();
    } else {
      callback(function);
    }
    return mlir::WalkResult::skip();
  });
}

inline constexpr llvm::StringLiteral nativeStringGlobalAttr =
    "native.string_global";
inline constexpr llvm::StringLiteral nativeScanPrefixGlobalAttr =
    "native.scan_prefix_global";
inline constexpr llvm::StringLiteral nativeFileScanPrefixGlobalAttr =
    "native.file_scan_prefix_global";
inline constexpr llvm::StringLiteral nativeAggregateBitstreamPlanGlobalAttr =
    "native.aggregate_bitstream_plan_global";
inline constexpr llvm::StringLiteral nativeContainerPatternPlanGlobalAttr =
    "native.container_pattern_plan_global";
inline constexpr llvm::StringLiteral nativeContainerPatternPlanSizeAttr =
    "native.container_pattern_plan_size";

inline constexpr llvm::StringLiteral nativeTwoStateBlockUnknownsAttr =
    "native.two_state_block_unknowns";
inline constexpr llvm::StringLiteral nativeInBoundsHandleOffsetAttr =
    "native.in_bounds_handle_offset";
inline constexpr llvm::StringLiteral managedRootRangeRecordAttr =
    "native.managed_root_range_record";
inline constexpr llvm::StringLiteral managedRootRangePushCheckAttr =
    "native.managed_root_range_push_check";
/// Ends the prologue that reacquires an activation's root record at a resume
/// target; coroutine lowering runs it only on scheduler resumption.
inline constexpr llvm::StringLiteral managedRootActivationPrologueAttr =
    "native.managed_root_activation_prologue";
inline constexpr llvm::StringLiteral nativeMethodArgumentSizesAttr =
    "native.method_argument_sizes";
inline constexpr llvm::StringLiteral nativeMethodArgumentRootsAttr =
    "native.method_argument_roots";
inline constexpr llvm::StringLiteral nativeTransferredReferencesAttr =
    "native.transferred_references";
inline constexpr auto assumeCleanSpecializationAttr =
    ::obelisk::schedule::Field::NativeAssumeCleanSpecialization;
inline constexpr auto cleanEvalBodyAttr =
    ::obelisk::schedule::Field::NativeCleanEvalBody;

struct NativeStateLayout;
NativeStateLayout makeCleanEvalStateLayout(const NativeStateLayout &layout);
inline constexpr llvm::StringLiteral evalCheckpointActorName =
    "__obelisk_eval_checkpoint_actor_v1";
inline constexpr llvm::StringLiteral evalCheckpointContinuationName =
    "__obelisk_eval_checkpoint_continuation_v1";
inline constexpr llvm::StringLiteral evalCheckpointCallbackName =
    "__obelisk_eval_checkpoint_callback_v1";
inline constexpr llvm::StringLiteral evalCheckpointMutableStateName =
    "__obelisk_eval_checkpoint_mutable_state_v1";
inline constexpr llvm::StringLiteral evalDispatchName =
    "__obelisk_eval_dispatch_v1";
inline constexpr auto evalRuntimeNBARequiredAttr =
    ::obelisk::schedule::Field::EvalRuntimeNbaRequired;
inline constexpr auto evalRuntimeNBAFallbackAttr =
    ::obelisk::schedule::Field::EvalRuntimeNbaFallback;

enum class NativeReturnLowering {
  None,
  Preserve,
  SuccessStatus,
};

enum class NativeCallResultLowering {
  Preserve,
  ConvertProcessTypes,
};

struct SignedI64Index {
  mlir::Value value;
  mlir::Value representable;
};

struct NativeStateLayout : analysis::NativeStateLayoutAnalysis {
  std::shared_ptr<const PackedStorageLayout> packedStorage;
  llvm::DenseSet<uint32_t> directHandles;
  llvm::DenseSet<uint32_t> guardedHandles;
  llvm::DenseSet<uint32_t> nbaHandles;
  llvm::DenseSet<uint32_t> transitionHandles;
  // Inferred waveform, source domain, and update cadence, independent of
  // calendar ownership. Tick-driven signals need not have periodic waveforms.
  llvm::DenseMap<analysis::ClockBit, analysis::ClockFact> clockFacts;
  bool hasClockTickBound(uint32_t state, uint64_t bit) const {
    auto fact = clockFacts.find({state, bit});
    return fact != clockFacts.end() && fact->second.hasTickBound();
  }
  bool transitionHandlesExact = false;
  bool directContinuous = false;
};

struct DirectStaticStateRange {
  uint64_t offset;
  uint64_t localOffset;
  uint32_t staticID;
  bool guarded;
  unsigned containerWidth = 0;
  std::optional<PackedAccessUnit> accessUnit;
};

struct NativeStateAccessInfo {
  uint32_t root;
  bool inBounds;
  // A typed element is contained whenever its enclosing index guards pass.
  bool wholeOrInvalid;
  std::optional<PackedAccessUnit> accessUnit;
};

bool emitDirectDynamicPackedStore(
    mlir::ConversionPatternRewriter &rewriter, mlir::Location location,
    mlir::Value handle, mlir::Value value, mlir::Value unknown,
    const NativeStateLayout *layout, bool assumeClean, bool continuous,
    bool twoState, schedule::SourceOwnerAttr sourceOwner,
    const NativeStateAccessInfo *access = nullptr);

/// Compiler-side field indices for obelisk_rt_native_schedule_plan. Keep all
/// LLVM literal construction and aggregate access tied to one named layout.
enum class NativeSchedulePlanField : int64_t {
  Size = 0,
  GraphLayoutChecksum,
  MutableState,
  MutableStateSize,
  ActorCapacity,
  Flags,
  StateValue,
  StateUnknown,
  StateBitCount,
  Bind,
  Run,
  FallbackSnapshot,
  NBARoots,
  NBARootCount,
  Reserved0,
  NBASites,
  NBASiteCount,
  FanoutEntries,
  FanoutEntryCount,
  ActorRoots,
  ActorRootCount,
  NBACommit,
  SpecializationFast,
  NBADirtyRoots,
  NBADirtyWordCount,
  Reserved1,
  NBADirtySummary,
  NBADirtySummaryWordCount,
  Reserved2,
  ClockKernels,
  ClockKernelCount,
  Reserved3,
  MergedFragments,
  MergedFragmentCount,
  TimeslotCoordinator,
  PromotionInvalidate,
  PromotionReady,
  PromotionInvalidateRange,
  PromotionRecheckRange,
  NBAOrderedCount,
  NBAOrderedBridge,
  Count,
};

mlir::LogicalResult materializeNativePromotionRangeIndex(mlir::ModuleOp module);
mlir::LogicalResult materializeNativePromotionWrites(mlir::ModuleOp module);
bool materializeNativeGroupDataflow(mlir::LLVM::LLVMFuncOp function,
                                    mlir::SymbolTable &symbols,
                                    uint64_t budget);
/// Refine a rejected collapsed group into ordered children. Leaves keep their
/// source activation identities; parents own no additional ready/proof state.
/// Returns original/candidate pairs, or an empty vector without changing the
/// original executor when the single-entry activation boundaries are unproved.
mlir::SmallVector<mlir::LLVM::LLVMFuncOp>
splitNativeEvalGroup(mlir::LLVM::LLVMFuncOp original,
                     mlir::LLVM::LLVMFuncOp candidate,
                     mlir::SymbolTable &symbols, uint64_t &budget);

mlir::LLVM::LLVMStructType
getNativeSchedulePlanLLVMType(mlir::MLIRContext *context);
uint64_t getNativeSchedulePlanSize(const llvm::DataLayout &dataLayout);

using ReferenceArgumentMap =
    llvm::DenseMap<mlir::Operation *, mlir::SmallVector<unsigned>>;

bool alignUp(uint64_t value, uint64_t alignment, uint64_t &result);
bool containsLogic(mlir::Type type);
std::optional<unsigned> nativeStateWidth(mlir::Type type);
mlir::Type convertProcessType(mlir::Type type, mlir::MLIRContext *context);
mlir::SmallVector<mlir::Value> flatten(mlir::ArrayRef<mlir::ValueRange> ranges);
mlir::Value llvmConstant(mlir::OpBuilder &builder, mlir::Location location,
                         mlir::Type type, uint64_t value);
mlir::Value entryAlloca(mlir::OpBuilder &builder, mlir::Location location,
                        mlir::Type elementType, uint64_t count,
                        unsigned alignment);
/// An entry-block buffer whose contents live only from the insertion point to
/// the matching endTemporary. A large body has thousands of runtime-call
/// buffers; lifetime markers let LLVM color them into a few stack slots.
mlir::Value beginTemporary(mlir::OpBuilder &builder, mlir::Location location,
                           mlir::Type elementType, unsigned alignment);
void endTemporary(mlir::OpBuilder &builder, mlir::Location location,
                  mlir::Value storage);
mlir::Value byteGEP(mlir::OpBuilder &builder, mlir::Location location,
                    mlir::Value base, uint64_t offset);
mlir::Value elementGEP(mlir::OpBuilder &builder, mlir::Location location,
                       mlir::Value base, mlir::Type elementType,
                       uint64_t index);
mlir::Value fieldGEP(mlir::OpBuilder &builder, mlir::Location location,
                     mlir::Value base, mlir::Type structureType,
                     uint32_t field);
mlir::Value loadAt(mlir::OpBuilder &builder, mlir::Location location,
                   mlir::Value base, uint64_t offset, mlir::Type type,
                   unsigned alignment);
void storeAt(mlir::OpBuilder &builder, mlir::Location location,
             mlir::Value base, uint64_t offset, mlir::Value value,
             unsigned alignment);
mlir::Value loadAt(mlir::OpBuilder &builder, mlir::Location location,
                   mlir::Value base, ProcessInstanceField field,
                   mlir::Type type, unsigned alignment);
void storeAt(mlir::OpBuilder &builder, mlir::Location location,
             mlir::Value base, ProcessInstanceField field, mlir::Value value,
             unsigned alignment);
mlir::Value loadAt(mlir::OpBuilder &builder, mlir::Location location,
                   mlir::Value base, FragmentActionField field, mlir::Type type,
                   unsigned alignment);
void storeAt(mlir::OpBuilder &builder, mlir::Location location,
             mlir::Value base, FragmentActionField field, mlir::Value value,
             unsigned alignment);
mlir::Value castIntegerWidth(mlir::OpBuilder &builder, mlir::Location location,
                             mlir::Value value, mlir::Type target);
mlir::Value asI64(mlir::OpBuilder &builder, mlir::Location location,
                  mlir::Value value);
mlir::Value resizeNativeInteger(mlir::OpBuilder &builder,
                                mlir::Location location, mlir::Value value,
                                mlir::IntegerType result,
                                bool isSigned = false);
SignedI64Index resizeSignedIndexToI64(mlir::OpBuilder &builder,
                                      mlir::Location location,
                                      mlir::Value source);
mlir::Value insertValue(mlir::OpBuilder &builder, mlir::Location location,
                        mlir::Value aggregate, mlir::Value element,
                        int64_t index);
mlir::Value insertValue(mlir::OpBuilder &builder, mlir::Location location,
                        mlir::Value aggregate, mlir::Value element,
                        NativeSchedulePlanField field);
void emitNativeStateRetain(mlir::OpBuilder &builder, mlir::Location location,
                           mlir::Value handle);
mlir::Operation *reportManagedStatus(mlir::OpBuilder &builder,
                                     mlir::Location location,
                                     mlir::Value context, mlir::Value status);
mlir::Value managedObjectPointer(mlir::OpBuilder &builder,
                                 mlir::Location location, mlir::Value handle);
mlir::Value managedObjectHandle(mlir::OpBuilder &builder,
                                mlir::Location location, mlir::Value object);
mlir::Value managedContext(mlir::OpBuilder &builder, mlir::Location location);
std::pair<mlir::Value, mlir::Value>
managedContextAndLane(mlir::OpBuilder &builder, mlir::Location location);
mlir::Value makeNativeAssocKey(mlir::OpBuilder &builder,
                               mlir::Location location,
                               sim::AssocArrayType array,
                               mlir::ValueRange values);
mlir::Value zeroNativeValue(mlir::OpBuilder &builder, mlir::Location location,
                            mlir::Type type);

std::string managedClassDescriptorName(mlir::SymbolRefAttr className);
std::string managedMethodThunkName(llvm::StringRef methodName);
mlir::LLVM::GlobalOp makeByteArrayGlobal(mlir::ModuleOp module,
                                         mlir::Location location,
                                         llvm::StringRef name,
                                         llvm::StringRef bytes);
mlir::LLVM::GlobalOp makeConstantGlobal(
    mlir::ModuleOp module, mlir::Location location, mlir::Type type,
    llvm::StringRef name, mlir::LLVM::Linkage linkage, uint64_t alignment,
    llvm::function_ref<mlir::Value(mlir::OpBuilder &)> initializer);

mlir::LLVM::LLVMFuncOp
getOrDeclareLLVMFunction(mlir::ModuleOp module, llvm::StringRef name,
                         mlir::Type result,
                         mlir::ArrayRef<mlir::Type> arguments);

/// Materialize the checkpoint handoff tuple referenced by Eval checkpoint
/// wrappers. Safe to call from both wrapper and accepted-plan construction.
mlir::LogicalResult
materializeEvalCheckpointHandoffGlobals(mlir::ModuleOp module);

/// Preserve the stable semantic native-partition boundary while replacing or
/// outlining executable functions during native lowering.
void copyNativePartition(mlir::Operation *source, mlir::Operation *target);

/// Build the link-complete physical partition manifest after every native
/// function, helper, and global has been materialized.
struct NativePhysicalPartitions;
mlir::FailureOr<std::shared_ptr<NativePhysicalPartitions>>
prepareNativePartitionManifest(mlir::ModuleOp module,
                               mlir::AnalysisManager manager);
std::unique_ptr<mlir::Pass> createInventoryNativeFunctionSymbolsPass();
mlir::LogicalResult
publishNativePartitionManifest(mlir::ModuleOp module,
                               const NativePhysicalPartitions &inventory);

mlir::LogicalResult lowerNativeDPICalls(mlir::Operation *root);
void materializeNativeSpawnBatches(mlir::Operation *root);
mlir::LogicalResult
lowerNativeFunctionBody(mlir::Operation *root,
                        NativeReturnLowering returnLowering,
                        NativeCallResultLowering callResultLowering);
mlir::LogicalResult
threadProcessStateThroughCFG(obelisk::sim::SimFuncOp function);
mlir::LogicalResult threadRuntimeStatuses(mlir::ModuleOp module,
                                          mlir::AnalysisManager manager);
void populateNativeManagedRootToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns);
void emitManagedRootRangePop(mlir::OpBuilder &builder, mlir::Location location,
                             mlir::Operation *scope);
/// The scope's managed-root range record, if it has one. Callers popping at
/// many exits look it up once and pass it to the overload below.
mlir::LLVM::AllocaOp findManagedRootRangeRecord(mlir::Operation *scope);
void emitManagedRootRangePop(mlir::OpBuilder &builder, mlir::Location location,
                             mlir::Operation *scope,
                             mlir::LLVM::AllocaOp record);
mlir::LogicalResult materializeDPIThunks(mlir::ModuleOp module);
mlir::LogicalResult materializeDPIExportWrappers(mlir::ModuleOp module);
mlir::LogicalResult materializeNativeDPIExportThunks(mlir::ModuleOp module);
/// Immutable class metadata resolved once before parallel function conversion.
/// Operation keys survive until their corresponding conversion pattern runs.
struct NativeClassLowering {
  struct Dispatch {
    uint64_t ownerID = 0;
    uint64_t interfaceOrdinal = 0;
    bool isInterface = false;
  };
  llvm::DenseMap<mlir::Operation *, uint64_t> fieldOffsets;
  llvm::DenseMap<mlir::Operation *, Dispatch> dispatches;
};
mlir::FailureOr<NativeClassLowering>
collectNativeClassLowering(mlir::ModuleOp module);
void populateManagedToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter,
    const llvm::DataLayout &dataLayout, uint64_t stateBitCount,
    const NativeClassLowering &classes);
void populateManagedStringToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter);
void populateManagedContainerToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter,
    const llvm::DataLayout &dataLayout);
void populateManagedAssociativeToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter);
void populateManagedCoverageToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter,
    const llvm::DataLayout &dataLayout, uint64_t stateBitCount);
void populateManagedReferenceToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter,
    const llvm::DataLayout &dataLayout, uint64_t stateBitCount);
void populateAggregateToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter);
void populateControlToLLVMConversionPatterns(mlir::RewritePatternSet &patterns,
                                             mlir::TypeConverter &converter);
void populateFunctionTypeConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter,
    const llvm::DenseSet<mlir::Value> &twoStateValues);
void populateEventToLLVMConversionPatterns(mlir::RewritePatternSet &patterns,
                                           mlir::TypeConverter &converter);
void populateSuspensionTypeConversionPatterns(mlir::RewritePatternSet &patterns,
                                              mlir::TypeConverter &converter);
class LocalStorageLowering;
void populateNativeHandleConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter,
    const llvm::DenseMap<uint64_t, uint64_t> &storageHandles,
    const llvm::DenseMap<uint64_t, uint64_t> &netHandles,
    const llvm::DenseMap<uint64_t, uint64_t> &driverHandles,
    const LocalStorageLowering *locals = nullptr,
    const llvm::DenseSet<mlir::Operation *> *inBoundsSelections = nullptr);
void populateOverrideToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter,
    uint64_t stateBitCount, const llvm::DataLayout &dataLayout);
void populateReferenceLifetimeToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter);
void populateSchedulerToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter);
void populateStateReadWriteToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter,
    uint64_t stateBitCount, const NativeStateLayout *directLayout,
    bool experimentalTwoState,
    const llvm::DenseMap<mlir::Operation *, NativeStateAccessInfo>
        &stateAccesses,
    const LocalStorageLowering *locals = nullptr);
void annotateStaticDriverNets(mlir::ModuleOp module,
                              const NativeStateLayout &layout);
void populateDriverToLLVMConversionPatterns(mlir::RewritePatternSet &patterns,
                                            mlir::TypeConverter &converter,
                                            const NativeStateLayout &layout);
void materializeNativeSchedulerGlobals(mlir::ModuleOp module);
void markLikelyTrue(mlir::cf::CondBranchOp branch);
void recordStaticSpecializationCFGBlocks(
    mlir::ConversionPatternRewriter &rewriter, mlir::Block *head,
    unsigned newBlockCount);
mlir::Value staticSpecializationGuard(mlir::ConversionPatternRewriter &rewriter,
                                      mlir::Location location,
                                      uint32_t staticID, uint32_t flags);
mlir::Value
staticNBASpecializationGuard(mlir::ConversionPatternRewriter &rewriter,
                             mlir::Location location, uint32_t rootIndex,
                             bool guardedVisibility = false);
std::optional<DirectStaticStateRange>
resolveDirectStaticStateRange(mlir::Value handle, unsigned width,
                              const NativeStateLayout *layout);
std::optional<uint64_t> resolveCFGConstantInteger(mlir::Value value);
mlir::Value loadStatePlane(mlir::ConversionPatternRewriter &rewriter,
                           mlir::Location location, mlir::Value handle,
                           mlir::IntegerType resultType,
                           llvm::StringRef globalName, bool unknownFallback,
                           uint64_t stateBitCount,
                           const NativeStateLayout *directLayout = nullptr,
                           mlir::Value guardedPermission = {},
                           bool assumeClean = false,
                           const NativeStateAccessInfo *access = nullptr);
mlir::Value storeStatePlane(mlir::ConversionPatternRewriter &rewriter,
                            mlir::Location location, mlir::Value handle,
                            mlir::Value input, llvm::StringRef globalName,
                            uint64_t stateBitCount,
                            const NativeStateLayout *directLayout = nullptr,
                            mlir::Value guardedPermission = {},
                            bool assumeClean = false, bool trackChange = true,
                            bool continuous = false);
void notifySignal(
    mlir::ConversionPatternRewriter &builder, mlir::Location location,
    mlir::Value handle, uint64_t width, mlir::Value oldValue,
    mlir::Value oldUnknown, mlir::Value newValue, mlir::Value newUnknown,
    std::optional<DirectStaticStateRange> directRange = std::nullopt,
    schedule::SourceOwnerAttr sourceOwner = {});
mlir::LogicalResult
insertAutomaticOwnerReleases(obelisk::sim::SimFuncOp function);
mlir::LogicalResult
releaseNativeAutomaticState(mlir::ModuleOp module,
                            const ReferenceArgumentMap &referenceArguments);
void populateContextRuntimeToLLVMConversionPattern(
    mlir::RewritePatternSet &patterns, const mlir::TypeConverter &converter);
mlir::LogicalResult
materializeManagedMethodThunks(mlir::ModuleOp module,
                               const llvm::DataLayout &dataLayout);
mlir::LogicalResult materializeNativeObserverThunks(mlir::ModuleOp module);
mlir::Operation *getConvertedObserverBinding(mlir::Value value);
uint32_t getConvertedObserverCaptureCount(mlir::Operation *binding);
mlir::Operation::operand_range
getConvertedObserverCaptures(mlir::Operation *binding);
mlir::Operation::operand_range
getConvertedObserverDependencies(mlir::Operation *binding);
mlir::LogicalResult serializeComputedObserverRecord(
    mlir::Operation *operation, llvm::ArrayRef<mlir::Operation *> bindings,
    llvm::ArrayRef<mlir::ValueRange> initialPlanes,
    llvm::ArrayRef<int32_t> edges, llvm::ArrayRef<int32_t> conditionIndices,
    mlir::Value wait, uint64_t waitSize, mlir::OpBuilder &builder,
    mlir::SmallVectorImpl<mlir::Operation *> &observerBindings);
mlir::LogicalResult serializeComputedObserverWait(
    mlir::Operation *operation, mlir::Value wait, uint64_t waitSize,
    mlir::OpBuilder &builder,
    mlir::SmallVectorImpl<mlir::Operation *> &observerBindings);
mlir::LogicalResult serializeRuntimeWait(
    mlir::Operation *operation, mlir::Value wait, uint32_t kind, uint32_t count,
    mlir::OpBuilder &builder,
    mlir::SmallVectorImpl<mlir::Operation *> &observerBindings);
uint64_t stableProcessID(llvm::StringRef name);
/// Shares symbol resolution and callee verdicts across many processes.
struct UnmanagedNativeCache {
  mlir::SymbolTableCollection symbols;
  llvm::DenseMap<mlir::Operation *, bool> bodies;
};
/// Whether a process holds only scalar state and calls only unmanaged
/// functions, so its activations need no managed root scope. This reads
/// callee bodies: call it only where no function worker rewrites them.
bool isUnmanagedNativeProcess(sim::SimFuncOp function,
                              UnmanagedNativeCache &cache);
/// The same proof with every call treated as managed. It reads only
/// `function`, so a function-pass worker may use it.
bool isCallFreeUnmanagedNativeProcess(sim::SimFuncOp function);
mlir::LogicalResult
makeProcessDescriptor(mlir::ModuleOp module, mlir::SymbolTable &embeddedSymbols,
                      mlir::Location location, llvm::StringRef baseName,
                      uint64_t stableID,
                      const SimulationProcessFrameAnalysis &analysis,
                      bool unmanagedNative = false, bool usesCoroutine = true,
                      bool tableProcess = false);
mlir::LogicalResult prepareManagedLowering(mlir::ModuleOp module,
                                           const llvm::DataLayout &dataLayout);
mlir::LogicalResult makeSchedulerMain(mlir::ModuleOp module,
                                      const NativeStateLayout &stateLayout,
                                      bool useAOT, bool directEval,
                                      bool hasObserver);
void declareNativeRuntimeABI(mlir::ModuleOp module);
mlir::FailureOr<NativeStateLayout>
buildNativeStateLayout(mlir::ModuleOp module,
                       const llvm::DataLayout &dataLayout);
void materializeNativeStatePlanes(mlir::ModuleOp module,
                                  const NativeStateLayout &layout);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_PRIVATE_H
