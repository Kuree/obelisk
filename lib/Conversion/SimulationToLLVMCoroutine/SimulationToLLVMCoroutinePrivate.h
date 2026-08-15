//===- SimulationToLLVMCoroutinePrivate.h - Shared lowering support ------===//

#ifndef OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_PRIVATE_H
#define OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_PRIVATE_H

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
#include <optional>
#include <string>
#include <utility>

namespace llvm {
class DataLayout;
}

namespace mlir {
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

inline constexpr llvm::StringLiteral nativeStringGlobalAttr =
    "obelisk.native.string_global";
inline constexpr llvm::StringLiteral nativeScanPrefixGlobalAttr =
    "obelisk.native.scan_prefix_global";
inline constexpr llvm::StringLiteral nativeFileScanPrefixGlobalAttr =
    "obelisk.native.file_scan_prefix_global";

inline constexpr llvm::StringLiteral nativeTwoStateBlockUnknownsAttr =
    "obelisk.native.two_state_block_unknowns";
inline constexpr llvm::StringLiteral managedRootRangeRecordAttr =
    "obelisk.managed_root_range_record";
inline constexpr llvm::StringLiteral managedRootRangePushCheckAttr =
    "obelisk.managed_root_range_push_check";
inline constexpr llvm::StringLiteral nativeMethodArgumentSizesAttr =
    "obelisk.native.method_argument_sizes";
inline constexpr llvm::StringLiteral nativeMethodArgumentRootsAttr =
    "obelisk.native.method_argument_roots";
inline constexpr llvm::StringLiteral nativeTransferredReferencesAttr =
    "obelisk.native.transferred_references";
inline constexpr llvm::StringLiteral assumeCleanSpecializationAttr =
    "obelisk.native.assume_clean_specialization";
inline constexpr llvm::StringLiteral evalCheckpointActorName =
    "__obelisk_eval_checkpoint_actor_v1";
inline constexpr llvm::StringLiteral evalCheckpointContinuationName =
    "__obelisk_eval_checkpoint_continuation_v1";
inline constexpr llvm::StringLiteral evalCheckpointCallbackName =
    "__obelisk_eval_checkpoint_callback_v1";
inline constexpr llvm::StringLiteral evalCheckpointMutableStateName =
    "__obelisk_eval_checkpoint_mutable_state_v1";
inline constexpr llvm::StringLiteral evalHybridCoordinatorName =
    "__obelisk_eval_fast_coordinator_hybrid_v1";

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
  llvm::DenseSet<uint32_t> directHandles;
  llvm::DenseSet<uint32_t> guardedHandles;
  llvm::DenseSet<uint32_t> nbaHandles;
  llvm::DenseSet<uint32_t> transitionHandles;
  bool transitionHandlesExact = false;
};

struct DirectStaticStateRange {
  uint64_t offset;
  uint64_t localOffset;
  uint32_t staticID;
  bool guarded;
};

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
  Count,
};

mlir::LLVM::LLVMStructType
getNativeSchedulePlanLLVMType(mlir::MLIRContext *context);

using ReferenceArgumentMap =
    llvm::DenseMap<mlir::Operation *, mlir::SmallVector<unsigned>>;

bool alignUp(uint64_t value, uint64_t alignment, uint64_t &result);
mlir::LogicalResult validateProcessABI(mlir::ModuleOp module,
                                       const llvm::DataLayout &layout);
bool containsLogic(mlir::Type type);
std::optional<unsigned> nativeStateWidth(mlir::Type type);
mlir::Type convertProcessType(mlir::Type type, mlir::MLIRContext *context);
mlir::SmallVector<mlir::Value> flatten(mlir::ArrayRef<mlir::ValueRange> ranges);
mlir::Value llvmConstant(mlir::OpBuilder &builder, mlir::Location location,
                         mlir::Type type, uint64_t value);
mlir::Value entryAlloca(mlir::OpBuilder &builder, mlir::Location location,
                        mlir::Type elementType, uint64_t count,
                        unsigned alignment);
mlir::Value byteGEP(mlir::OpBuilder &builder, mlir::Location location,
                    mlir::Value base, uint64_t offset);
mlir::Value loadAt(mlir::OpBuilder &builder, mlir::Location location,
                   mlir::Value base, uint64_t offset, mlir::Type type,
                   unsigned alignment);
void storeAt(mlir::OpBuilder &builder, mlir::Location location,
             mlir::Value base, uint64_t offset, mlir::Value value,
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

mlir::LogicalResult lowerNativeDPICalls(mlir::Operation *root);
mlir::LogicalResult
lowerNativeFunctionBody(mlir::Operation *root,
                        NativeReturnLowering returnLowering,
                        NativeCallResultLowering callResultLowering);
mlir::LogicalResult
threadProcessStateThroughCFG(obelisk::sim::SimFuncOp function);
mlir::LogicalResult threadRuntimeStatuses(mlir::ModuleOp module);
mlir::LogicalResult instrumentManagedRoots(mlir::ModuleOp module);
void emitManagedRootRangePop(mlir::OpBuilder &builder, mlir::Location location,
                             mlir::Operation *scope);
mlir::LogicalResult materializeDPIThunks(mlir::ModuleOp module);
void populateManagedToLLVMConversionPatterns(mlir::RewritePatternSet &patterns,
                                             mlir::TypeConverter &converter,
                                             const llvm::DataLayout &dataLayout,
                                             uint64_t stateBitCount);
void populateManagedStringToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter);
void populateManagedContainerToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter);
void populateManagedAssociativeToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter);
void populateManagedCoverageToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter);
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
void populateNativeHandleConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter,
    const llvm::DenseMap<uint64_t, uint64_t> &storageHandles,
    const llvm::DenseMap<uint64_t, uint64_t> &netHandles,
    const llvm::DenseMap<uint64_t, uint64_t> &driverHandles);
void populateOverrideToLLVMConversionPatterns(mlir::RewritePatternSet &patterns,
                                              mlir::TypeConverter &converter,
                                              uint64_t stateBitCount);
void populateReferenceLifetimeToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter);
void populateSchedulerToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter);
void populateStateReadWriteToLLVMConversionPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &converter,
    uint64_t stateBitCount, const NativeStateLayout *directLayout,
    bool experimentalTwoState);
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
                             mlir::Location location, uint32_t rootIndex);
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
                           bool assumeClean = false);
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
    std::optional<DirectStaticStateRange> directRange = std::nullopt);
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
mlir::LogicalResult serializeComputedObserverWait(
    mlir::Operation *operation, mlir::Value wait, uint64_t waitSize,
    mlir::OpBuilder &builder,
    mlir::SmallVectorImpl<mlir::Operation *> &observerBindings);
mlir::LogicalResult serializeRuntimeWait(mlir::Operation *operation,
                                         mlir::Value wait, uint32_t kind,
                                         uint32_t count,
                                         mlir::OpBuilder &builder);
uint64_t stableProcessID(llvm::StringRef name);
mlir::LogicalResult
makeProcessDescriptor(mlir::ModuleOp module, mlir::Location location,
                      llvm::StringRef baseName, uint64_t stableID,
                      const SimulationProcessFrameAnalysis &analysis);
mlir::LogicalResult prepareManagedLowering(mlir::ModuleOp module,
                                           const llvm::DataLayout &dataLayout);
mlir::LogicalResult makeSchedulerMain(mlir::ModuleOp module,
                                      const NativeStateLayout &stateLayout,
                                      bool useAOT, bool directEval);
void declareNativeRuntimeABI(mlir::ModuleOp module);
mlir::FailureOr<NativeStateLayout>
buildNativeStateLayout(mlir::ModuleOp module);
mlir::LLVM::GlobalOp makeStatePlane(mlir::ModuleOp module, llvm::StringRef name,
                                    uint64_t bytes, bool unknown,
                                    const NativeStateLayout &layout);

} // namespace obelisk::detail

#endif // OBELISK_LIB_CONVERSION_SIMULATIONTOLLVMCOROUTINE_PRIVATE_H
