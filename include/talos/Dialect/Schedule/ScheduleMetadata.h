#ifndef TALOS_DIALECT_SCHEDULE_METADATA_H
#define TALOS_DIALECT_SCHEDULE_METADATA_H
#include "talos/Dialect/Schedule/ScheduleFieldEnums.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
namespace talos::schedule::metadata {
inline constexpr auto nativeGuardedSpecializationBody =
    Field::NativeGuardedSpecializationBody;
inline constexpr auto nativeRegionBody = Field::NativeRegionBody;

// Scheduling metadata is keyed by the generated, typed Field enum.
/// All transient late-lowering metadata is revision-coupled and uses one
/// schema. Consumers reject stale IR instead of maintaining parallel readers.
inline constexpr uint32_t schemaVersion = 1;
inline constexpr uint32_t maxDirectStaticStateBits = 64;

/// Process whose same-edge waits have one explicit, storage-backed
/// phase dispatcher. Its entry is cold startup, not an eval activation
/// preamble.
inline constexpr auto clockedControl =
    ::talos::schedule::Field::ClockedControl;
/// Uniform delay process normalized to private periodic clock activations.
inline constexpr auto periodicControl =
    ::talos::schedule::Field::PeriodicControl;

/// Storage descriptors whose intermediate NBA updates are observable, as a
/// sorted i64 array on the design. Every other storage root may merge its
/// NBA updates to one final transition. Absence means every root is
/// observable. Computed once, before body fusion changes process shapes.
inline constexpr auto nbaTransientObservable =
    ::talos::schedule::Field::NbaTransientObservable;
/// Storage descriptors watched only by waits for any change, as a sorted i64
/// array beside nbaTransientObservable. These may merge NBA updates only when
/// every merge also records rewritten bits in a transient mask. Absence with
/// nbaTransientObservable present means none.
inline constexpr auto nbaChangeWatched =
    ::talos::schedule::Field::NbaChangeWatched;
inline constexpr auto staticBodyFusion =
    ::talos::schedule::Field::StaticBodyFusion;
inline constexpr auto staticFusion = ::talos::schedule::Field::StaticFusion;
/// Termination proof for a constant-induction, non-suspending CFG loop. Set on
/// the latch branch that closes the loop and on the header's conditional
/// branch. Carries no iteration count: it records only that the loop provably
/// exits, so schedule-group classification need not treat the backedge as an
/// unbounded control loop. Independent of whether the unroller replicated the
/// body.
inline constexpr auto boundedLoopLatch =
    ::talos::schedule::Field::BoundedLoopLatch;
inline constexpr auto boundedLoopHeader =
    ::talos::schedule::Field::BoundedLoopHeader;
inline constexpr auto computeKernels =
    ::talos::schedule::Field::ComputeKernels;
inline constexpr auto threeTierSchedule =
    ::talos::schedule::Field::ThreeTierSchedule;
inline constexpr auto staticSpecialization =
    ::talos::schedule::Field::StaticSpecialization;
inline constexpr auto staticSuperstep =
    ::talos::schedule::Field::StaticSuperstep;
/// Marks the outer implicit wait of an `always @*` process.
inline constexpr auto topLevelWildcardWait =
    ::talos::schedule::Field::TopLevelWildcardWait;
/// Marks a source-language procedural event control. Its controlled statement
/// executes after the wait and cannot reactivate that same wait from within
/// the active logical process.
inline constexpr auto proceduralEventWait =
    ::talos::schedule::Field::ProceduralEventWait;
/// Marks an outer explicit event control of a general-purpose `always`
/// procedure. The procedure returns to this wait after every iteration, so an
/// event enabled by its body can enqueue the next iteration.
inline constexpr auto repeatingAlwaysWait =
    ::talos::schedule::Field::RepeatingAlwaysWait;
/// Logical DPI output-formal indices whose unused internal copy-in operands
/// were removed while preserving the externally visible DPI signature.
// Revision-coupled eval facts shared by planning and LLVM materialization.
// These affect scheduling correctness and must not drift as ad-hoc strings
// between producer and consumer modules.
inline constexpr auto evalTier2Convergence =
    ::talos::schedule::Field::EvalTier2Convergence;
inline constexpr auto evalMayTerminate =
    ::talos::schedule::Field::EvalMayTerminate;
inline constexpr auto evalInfallible =
    ::talos::schedule::Field::EvalInfallible;
/// A status-returning owner whose nonzero result is a fractured cold
/// checkpoint. The periodic prefix may call it directly when it checks that
/// status before running any downstream owner.
inline constexpr auto evalCheckpointSafe =
    ::talos::schedule::Field::EvalCheckpointSafe;
inline constexpr auto evalTwoStateVariant =
    ::talos::schedule::Field::EvalTwoStateVariant;
inline constexpr auto evalPathGuardedTwoState =
    ::talos::schedule::Field::EvalPathGuardedTwoState;
/// A path-guarded owner whose complete persistent state closure is known-
/// preserving. Once its recorded promotion ranges are known, the dispatcher
/// only needs to retain the checkpoint-path probe.
inline constexpr auto evalPathGuardedKnownPreserving =
    ::talos::schedule::Field::EvalPathGuardedKnownPreserving;
/// An owner whose route probe was declined, so no path predicate guards its
/// runtime leaf. Its four-state body calls the runtime inline and must stay
/// runtime-owned.
inline constexpr auto evalUnsupportedCheckpointOwner =
    ::talos::schedule::Field::EvalUnsupportedCheckpointOwner;
inline constexpr auto evalCallClosureRoot =
    ::talos::schedule::Field::EvalCallClosureRoot;
inline constexpr auto evalCheckpointRoutes =
    ::talos::schedule::Field::EvalCheckpointRoutes;
/// Producer certificate for a generated region activation that reconstructs
/// actor-side continuation arguments from canonical state on every entry.
inline constexpr auto evalReconstructsContinuationArgs =
    ::talos::schedule::Field::EvalReconstructsContinuationArgs;
/// Per-NBA conversion certificate that the selected generated owner may use
/// its fixed root/region metadata.  Attach this before dialect conversion;
/// conversion patterns must not rediscover the fact from a parent function
/// that another pattern may already have replaced.
inline constexpr auto evalCompactNBAMetadata =
    ::talos::schedule::Field::EvalCompactNbaMetadata;
/// Stable logical process identity attached to operations cloned into a
/// fused eval body.  The inliner propagates a call-site identity through
/// helper bodies so active-self suppression does not depend on the physical
/// coordinator that happens to contain the operation.
inline constexpr auto evalSourceOwner =
    ::talos::schedule::Field::EvalSourceOwner;
/// Inter-pass proof marker for a read-observable canonical store that a
/// dormant Tier-1 eval specialization may omit.  MaterializeComputeFusion
/// attaches it only after proving private dominating-store promotion;
/// SimulationToLLVMCoroutine consumes it while cloning the eval-private call
/// closure and removes it before dialect lowering.
inline constexpr auto evalDiscardableStore =
    ::talos::schedule::Field::EvalDiscardableStore;

// These structural annotations require their original operation boundary.
// Other fields describe transferable physical identity or derived eval proofs.
inline bool pinsOperationBoundary(Field field) {
  switch (field) {
  case Field::StartsWithoutWaiting:
  case Field::PrimeOnSpawn:
  case Field::DetachedControls:
  case Field::PrioritySignalResume:
  case Field::ProgramOwnerId:
  case Field::ConcurrentCancel:
  case Field::ConcurrentAbort:
  case Field::ConcurrentCancelLevelTrue:
  case Field::ConcurrentAbortLevelTrue:
  case Field::EventPrimary:
  case Field::ObserverWidth:
  case Field::ObserverFourState:
  case Field::OverrideEvaluator:
  case Field::ConcurrentEosCounted:
  case Field::ConcurrentReport:
  case Field::ConcurrentCancelObserver:
  case Field::ConcurrentAbortObserver:
  case Field::ConcurrentCancelObserverRequest:
  case Field::ConcurrentAbortObserverRequest:
  case Field::BoundedLoopHeader:
  case Field::BoundedLoopLatch:
  case Field::ClockedControl:
  case Field::PeriodicControl:
  case Field::ClockedSamplePlan:
  case Field::ObserverCaptureBridge:
  case Field::ConcurrentEosCoordinator:
  case Field::PrimitiveName:
  case Field::CovergroupClockingSampler:
  case Field::DeferNetResolution:
  case Field::OutlinedPrimitiveMember:
  case Field::OutlinedPrimitiveFingerprint:
  case Field::ExactDriverId:
  case Field::ExactDriverLow:
  case Field::ComputedEventStartup:
    return true;
  default:
    return false;
  }
}
inline bool requiresOperationScope(Field field) {
  if (pinsOperationBoundary(field))
    return true;
  switch (field) {
  case Field::ComputeKernels:
  case Field::StaticBodyFusion:
  case Field::StaticFusion:
  case Field::StaticSpecialization:
  case Field::StaticSuperstep:
  case Field::ThreeTierSchedule:
  case Field::TopLevelWildcardWait:
  case Field::ProceduralEventWait:
  case Field::RepeatingAlwaysWait:
    return true;
  default:
    return false;
  }
}
} // namespace talos::schedule::metadata
#endif
