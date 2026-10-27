//===- SimulationDialect.h - Executable simulation dialect -----*- C++ -*-===//

#ifndef TALOS_DIALECT_SIMULATION_SIMULATIONDIALECT_H
#define TALOS_DIALECT_SIMULATION_SIMULATIONDIALECT_H

#include "talos/Dialect/DiscardableAttributes.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Dialect.h"

// The dialect's attribute helpers name its attribute and type classes.
#include "talos/Dialect/Simulation/SimulationAttrs.h"

#include "talos/Dialect/Simulation/SimulationDialect.h.inc"

/// Typed handles of the declared `simulation.*` attributes, for use with
/// `sim::get`, `set`, `has`, `mark`, `remove` and `named`.
namespace talos::sim::metadata {

using IntegerRange = TalosSimulationDialect::IntegerRangeAttrHelper;
using ResultRanges = TalosSimulationDialect::ResultRangesAttrHelper;
using ResultExprs = TalosSimulationDialect::ResultExprsAttrHelper;
using KnownLogicResult = TalosSimulationDialect::KnownLogicResultAttrHelper;
using GCPolled = TalosSimulationDialect::GcPolledAttrHelper;
using InlineLegalityCache = TalosSimulationDialect::InlineLegalityAttrHelper;
using PulseRejectPercent =
    TalosSimulationDialect::PulseRejectPercentAttrHelper;
using PulseErrorPercent = TalosSimulationDialect::PulseErrorPercentAttrHelper;
using PulseOnDetect = TalosSimulationDialect::PulseOnDetectAttrHelper;
using PulseShowCancelled =
    TalosSimulationDialect::PulseShowCancelledAttrHelper;
using ConstraintMode = TalosSimulationDialect::ConstraintModeAttrHelper;
using ConstraintModeBlock =
    TalosSimulationDialect::ConstraintModeBlockAttrHelper;
using ConstraintModeField =
    TalosSimulationDialect::ConstraintModeFieldAttrHelper;
using ConstraintModeStaticStorage =
    TalosSimulationDialect::ConstraintModeStaticStorageAttrHelper;
using ConstraintModeStaticStorages =
    TalosSimulationDialect::ConstraintModeStaticStoragesAttrHelper;
using ObjectRandomDispatchClasses =
    TalosSimulationDialect::ObjectRandomDispatchClassesAttrHelper;
using RandMode = TalosSimulationDialect::RandModeAttrHelper;
using RandModeAncestorMask =
    TalosSimulationDialect::RandModeAncestorMaskAttrHelper;
using RandModeArrayField =
    TalosSimulationDialect::RandModeArrayFieldAttrHelper;
using RandModeArrayStorage =
    TalosSimulationDialect::RandModeArrayStorageAttrHelper;
using RandModeArrayType = TalosSimulationDialect::RandModeArrayTypeAttrHelper;
using RandModeFamilyMask =
    TalosSimulationDialect::RandModeFamilyMaskAttrHelper;
using RandModeProperty = TalosSimulationDialect::RandModePropertyAttrHelper;
using RandModeSelectionMask =
    TalosSimulationDialect::RandModeSelectionMaskAttrHelper;
using RandModeStaticDispatch =
    TalosSimulationDialect::RandModeStaticDispatchAttrHelper;
using RandModeStaticStorage =
    TalosSimulationDialect::RandModeStaticStorageAttrHelper;
using RandModeStructDepth =
    TalosSimulationDialect::RandModeStructDepthAttrHelper;
using RandModeStructElementFields =
    TalosSimulationDialect::RandModeStructElementFieldsAttrHelper;
using RandomArithmeticNode =
    TalosSimulationDialect::RandomArithmeticNodeAttrHelper;
using RandomArithmeticOperand =
    TalosSimulationDialect::RandomArithmeticOperandAttrHelper;
using RandomArithmeticPlane =
    TalosSimulationDialect::RandomArithmeticPlaneAttrHelper;
using RandomArrayElementWidth =
    TalosSimulationDialect::RandomArrayElementWidthAttrHelper;
using RandomArrayModeField =
    TalosSimulationDialect::RandomArrayModeFieldAttrHelper;
using RandomConstraintBlock =
    TalosSimulationDialect::RandomConstraintBlockAttrHelper;
using RandomConstraintCount =
    TalosSimulationDialect::RandomConstraintCountAttrHelper;
using RandomContainerProperties =
    TalosSimulationDialect::RandomContainerPropertiesAttrHelper;
using RandomCycleKeyField =
    TalosSimulationDialect::RandomCycleKeyFieldAttrHelper;
using RandomCyclePositionField =
    TalosSimulationDialect::RandomCyclePositionFieldAttrHelper;
using RandomEvaluationError =
    TalosSimulationDialect::RandomEvaluationErrorAttrHelper;
using RandomEvaluationInputs =
    TalosSimulationDialect::RandomEvaluationInputsAttrHelper;
using RandomFixedConstraintModes =
    TalosSimulationDialect::RandomFixedConstraintModesAttrHelper;
using RandomFunctionCaseCondition =
    TalosSimulationDialect::RandomFunctionCaseConditionAttrHelper;
using RandomFunctionConditional =
    TalosSimulationDialect::RandomFunctionConditionalAttrHelper;
using RandomFunctionExpression =
    TalosSimulationDialect::RandomFunctionExpressionAttrHelper;
using RandomFunctionOrder =
    TalosSimulationDialect::RandomFunctionOrderAttrHelper;
using RandomFunctionState =
    TalosSimulationDialect::RandomFunctionStateAttrHelper;
using RandomGuardInputs = TalosSimulationDialect::RandomGuardInputsAttrHelper;
using RandomIncrementField =
    TalosSimulationDialect::RandomIncrementFieldAttrHelper;
using RandomModeField = TalosSimulationDialect::RandomModeFieldAttrHelper;
using RandomModeIndex = TalosSimulationDialect::RandomModeIndexAttrHelper;
using RandomNestedConstraintModes =
    TalosSimulationDialect::RandomNestedConstraintModesAttrHelper;
using RandomNestedHooks = TalosSimulationDialect::RandomNestedHooksAttrHelper;
using RandomNestedStateConcreteType =
    TalosSimulationDialect::RandomNestedStateConcreteTypeAttrHelper;
using RandomNestedStateField =
    TalosSimulationDialect::RandomNestedStateFieldAttrHelper;
using RandomNestedStatePath =
    TalosSimulationDialect::RandomNestedStatePathAttrHelper;
using RandomNestedStateStorageType =
    TalosSimulationDialect::RandomNestedStateStorageTypeAttrHelper;
using RandomObjectEdge = TalosSimulationDialect::RandomObjectEdgeAttrHelper;
using EmbeddedCovergroup =
    TalosSimulationDialect::EmbeddedCovergroupAttrHelper;
using CheckerProcedure = TalosSimulationDialect::CheckerProcedureAttrHelper;
using RandomPostHook = TalosSimulationDialect::RandomPostHookAttrHelper;
using RandomPreHook = TalosSimulationDialect::RandomPreHookAttrHelper;
using RandomPriorityArguments =
    TalosSimulationDialect::RandomPriorityArgumentsAttrHelper;
using RandomPriorityInputs =
    TalosSimulationDialect::RandomPriorityInputsAttrHelper;
using RandomProperties = TalosSimulationDialect::RandomPropertiesAttrHelper;
using RandomRealFinite = TalosSimulationDialect::RandomRealFiniteAttrHelper;
using RandomReceiverIndex =
    TalosSimulationDialect::RandomReceiverIndexAttrHelper;
using RandomRecursiveAliasGuards =
    TalosSimulationDialect::RandomRecursiveAliasGuardsAttrHelper;
using RandomStateField = TalosSimulationDialect::RandomStateFieldAttrHelper;
using RandomStructMembers =
    TalosSimulationDialect::RandomStructMembersAttrHelper;
using RandomTotalWidth = TalosSimulationDialect::RandomTotalWidthAttrHelper;
using RandomVariable = TalosSimulationDialect::RandomVariableAttrHelper;
using RandomVariableBitOffset =
    TalosSimulationDialect::RandomVariableBitOffsetAttrHelper;
using RandomVariableKind =
    TalosSimulationDialect::RandomVariableKindAttrHelper;
using RandomVariableSigned =
    TalosSimulationDialect::RandomVariableSignedAttrHelper;
using Randomize = TalosSimulationDialect::RandomizeAttrHelper;
using RandomizeCheckerOnly =
    TalosSimulationDialect::RandomizeCheckerOnlyAttrHelper;
using RandomizeConstraintTemplate =
    TalosSimulationDialect::RandomizeConstraintTemplateAttrHelper;
using RandomizeDispatch = TalosSimulationDialect::RandomizeDispatchAttrHelper;
using RandomizeExplicitProperties =
    TalosSimulationDialect::RandomizeExplicitPropertiesAttrHelper;
using RandomizeHelper = TalosSimulationDialect::RandomizeHelperAttrHelper;
using RandomizeHelperCaptures =
    TalosSimulationDialect::RandomizeHelperCapturesAttrHelper;
using RandomizeHelperReadCaptures =
    TalosSimulationDialect::RandomizeHelperReadCapturesAttrHelper;
using RandomizeHelperReceiver =
    TalosSimulationDialect::RandomizeHelperReceiverAttrHelper;
using RandomizeNestedDispatch =
    TalosSimulationDialect::RandomizeNestedDispatchAttrHelper;
using RandomizeNestedDispatchField =
    TalosSimulationDialect::RandomizeNestedDispatchFieldAttrHelper;
using RandomizeNestedDispatchPath =
    TalosSimulationDialect::RandomizeNestedDispatchPathAttrHelper;
using RandomizeNestedDispatchSelectionPath =
    TalosSimulationDialect::RandomizeNestedDispatchSelectionPathAttrHelper;
using RandomizeNestedDispatchStorageType =
    TalosSimulationDialect::RandomizeNestedDispatchStorageTypeAttrHelper;
using RandomizeNestedPlans =
    TalosSimulationDialect::RandomizeNestedPlansAttrHelper;
using RandomizePlanClass =
    TalosSimulationDialect::RandomizePlanClassAttrHelper;
using RandomizeScope = TalosSimulationDialect::RandomizeScopeAttrHelper;
using StaticConstraintStorage =
    TalosSimulationDialect::StaticConstraintStorageAttrHelper;
using StaticRandomArrayModeStorage =
    TalosSimulationDialect::StaticRandomArrayModeStorageAttrHelper;
using StaticRandomModeStorage =
    TalosSimulationDialect::StaticRandomModeStorageAttrHelper;
using AssertionActionControlled =
    TalosSimulationDialect::AssertionActionControlledAttrHelper;
using AssertionControlAction =
    TalosSimulationDialect::AssertionControlActionAttrHelper;
using AssertionControlActionArgument =
    TalosSimulationDialect::AssertionControlActionArgumentAttrHelper;
using AssertionControlAssertionTypes =
    TalosSimulationDialect::AssertionControlAssertionTypesAttrHelper;
using AssertionControlAssertionTypesArgument =
    TalosSimulationDialect::AssertionControlAssertionTypesArgumentAttrHelper;
using AssertionControlDepths =
    TalosSimulationDialect::AssertionControlDepthsAttrHelper;
using AssertionControlDirectiveTypes =
    TalosSimulationDialect::AssertionControlDirectiveTypesAttrHelper;
using AssertionControlDirectiveTypesArgument =
    TalosSimulationDialect::AssertionControlDirectiveTypesArgumentAttrHelper;
using AssertionControlIds =
    TalosSimulationDialect::AssertionControlIdsAttrHelper;
using AssertionControlLevelsArgument =
    TalosSimulationDialect::AssertionControlLevelsArgumentAttrHelper;
using AssertionControlTargetId =
    TalosSimulationDialect::AssertionControlTargetIdAttrHelper;
using AssertionControlled =
    TalosSimulationDialect::AssertionControlledAttrHelper;
using AssertionKillControlled =
    TalosSimulationDialect::AssertionKillControlledAttrHelper;
using AssertionLocalTypes =
    TalosSimulationDialect::AssertionLocalTypesAttrHelper;
using AssertionPath = TalosSimulationDialect::AssertionPathAttrHelper;
using AssertionTargetId = TalosSimulationDialect::AssertionTargetIdAttrHelper;
using AsynchronousPropertyAbort =
    TalosSimulationDialect::AsynchronousPropertyAbortAttrHelper;
using BoundedAntecedentHorizon =
    TalosSimulationDialect::BoundedAntecedentHorizonAttrHelper;
using BranchingAntecedentAlternatives =
    TalosSimulationDialect::BranchingAntecedentAlternativesAttrHelper;
using BranchingAntecedentBackedge =
    TalosSimulationDialect::BranchingAntecedentBackedgeAttrHelper;
using BranchingAntecedentChannel =
    TalosSimulationDialect::BranchingAntecedentChannelAttrHelper;
using BranchingAntecedentConsequentEosStrength = TalosSimulationDialect::
    BranchingAntecedentConsequentEosStrengthAttrHelper;
using BranchingAntecedentConsequentTrigger =
    TalosSimulationDialect::BranchingAntecedentConsequentTriggerAttrHelper;
using BranchingAntecedentEosCoalescer =
    TalosSimulationDialect::BranchingAntecedentEosCoalescerAttrHelper;
using BranchingAntecedentEosReport =
    TalosSimulationDialect::BranchingAntecedentEosReportAttrHelper;
using BranchingAntecedentEosResult =
    TalosSimulationDialect::BranchingAntecedentEosResultAttrHelper;
using BranchingAntecedentEosSourceAge =
    TalosSimulationDialect::BranchingAntecedentEosSourceAgeAttrHelper;
using BranchingAntecedentExistentialFailure =
    TalosSimulationDialect::BranchingAntecedentExistentialFailureAttrHelper;
using BranchingAntecedentExistentialSuccess =
    TalosSimulationDialect::BranchingAntecedentExistentialSuccessAttrHelper;
using BranchingAntecedentMatchChannels =
    TalosSimulationDialect::BranchingAntecedentMatchChannelsAttrHelper;
using BranchingAntecedentMatchedHistory =
    TalosSimulationDialect::BranchingAntecedentMatchedHistoryAttrHelper;
using BranchingAntecedentMonitor =
    TalosSimulationDialect::BranchingAntecedentMonitorAttrHelper;
using BranchingAntecedentResultCancel =
    TalosSimulationDialect::BranchingAntecedentResultCancelAttrHelper;
using BranchingAntecedentResultCoalescer =
    TalosSimulationDialect::BranchingAntecedentResultCoalescerAttrHelper;
using BranchingAntecedentResultHorizon =
    TalosSimulationDialect::BranchingAntecedentResultHorizonAttrHelper;
using BranchingAntecedentUniversalFailure =
    TalosSimulationDialect::BranchingAntecedentUniversalFailureAttrHelper;
using BranchingAntecedentUniversalSuccess =
    TalosSimulationDialect::BranchingAntecedentUniversalSuccessAttrHelper;
using BranchingAntecedentVacuity =
    TalosSimulationDialect::BranchingAntecedentVacuityAttrHelper;
using BranchingConsequentAlternative =
    TalosSimulationDialect::BranchingConsequentAlternativeAttrHelper;
using BranchingConsequentAlternativeCancel =
    TalosSimulationDialect::BranchingConsequentAlternativeCancelAttrHelper;
using BranchingConsequentAlternatives =
    TalosSimulationDialect::BranchingConsequentAlternativesAttrHelper;
using BranchingConsequentBackedge =
    TalosSimulationDialect::BranchingConsequentBackedgeAttrHelper;
using BranchingConsequentEosCoalescer =
    TalosSimulationDialect::BranchingConsequentEosCoalescerAttrHelper;
using BranchingConsequentIntrinsicEosStrength =
    TalosSimulationDialect::BranchingConsequentIntrinsicEosStrengthAttrHelper;
using BranchingConsequentMonitor =
    TalosSimulationDialect::BranchingConsequentMonitorAttrHelper;
using BranchingConsequentNonoverlapped =
    TalosSimulationDialect::BranchingConsequentNonoverlappedAttrHelper;
using BranchingConsequentTrigger =
    TalosSimulationDialect::BranchingConsequentTriggerAttrHelper;
using BranchingSequenceAlternatives =
    TalosSimulationDialect::BranchingSequenceAlternativesAttrHelper;
using BranchingSequenceMonitor =
    TalosSimulationDialect::BranchingSequenceMonitorAttrHelper;
using ClockingEventMonitor =
    TalosSimulationDialect::ClockingEventMonitorAttrHelper;
using ClockingEventMonitorPath =
    TalosSimulationDialect::ClockingEventMonitorPathAttrHelper;
using CombinedBooleanBranchingMonitor =
    TalosSimulationDialect::CombinedBooleanBranchingMonitorAttrHelper;
using CombinedBooleanBranchingPairs =
    TalosSimulationDialect::CombinedBooleanBranchingPairsAttrHelper;
using CombinedBooleanBranchingPairsBeforeMinimization =
    TalosSimulationDialect::
        CombinedBooleanBranchingPairsBeforeMinimizationAttrHelper;
using CombinedBoundedBranchingMonitor =
    TalosSimulationDialect::CombinedBoundedBranchingMonitorAttrHelper;
using CombinedBoundedBranchingPairs =
    TalosSimulationDialect::CombinedBoundedBranchingPairsAttrHelper;
using CombinedBoundedBranchingPairsBeforeMinimization =
    TalosSimulationDialect::
        CombinedBoundedBranchingPairsBeforeMinimizationAttrHelper;
using ConcurrentAbortCounted =
    TalosSimulationDialect::ConcurrentAbortCountedAttrHelper;
using ConcurrentActionClass =
    TalosSimulationDialect::ConcurrentActionClassAttrHelper;
using ConcurrentActionControl =
    TalosSimulationDialect::ConcurrentActionControlAttrHelper;
using ConcurrentAttemptActionState =
    TalosSimulationDialect::ConcurrentAttemptActionStateAttrHelper;
using ConcurrentAttemptActionStateLoad =
    TalosSimulationDialect::ConcurrentAttemptActionStateLoadAttrHelper;
using ConcurrentAttemptActionStateNext =
    TalosSimulationDialect::ConcurrentAttemptActionStateNextAttrHelper;
using ConcurrentAttemptActionStateStorage =
    TalosSimulationDialect::ConcurrentAttemptActionStateStorageAttrHelper;
using ConcurrentAttemptActionStateStore =
    TalosSimulationDialect::ConcurrentAttemptActionStateStoreAttrHelper;
using ConcurrentAttemptEnable =
    TalosSimulationDialect::ConcurrentAttemptEnableAttrHelper;
using ConcurrentAttemptStart =
    TalosSimulationDialect::ConcurrentAttemptStartAttrHelper;
using ConcurrentEosForcedCompletion =
    TalosSimulationDialect::ConcurrentEosForcedCompletionAttrHelper;
using ConcurrentEosReport =
    TalosSimulationDialect::ConcurrentEosReportAttrHelper;
using ConcurrentEosVacuous =
    TalosSimulationDialect::ConcurrentEosVacuousAttrHelper;
using ConcurrentKillEpoch =
    TalosSimulationDialect::ConcurrentKillEpochAttrHelper;
using ConcurrentKillEpochCheck =
    TalosSimulationDialect::ConcurrentKillEpochCheckAttrHelper;
using ConcurrentKillEpochStorage =
    TalosSimulationDialect::ConcurrentKillEpochStorageAttrHelper;
using ConcurrentMatchCall =
    TalosSimulationDialect::ConcurrentMatchCallAttrHelper;
using ConcurrentMatchCallChain =
    TalosSimulationDialect::ConcurrentMatchCallChainAttrHelper;
using ConcurrentMatchCallKillEpoch =
    TalosSimulationDialect::ConcurrentMatchCallKillEpochAttrHelper;
using ConcurrentReportKillEpoch =
    TalosSimulationDialect::ConcurrentReportKillEpochAttrHelper;
using ConsequentEndOfSimulationStrength =
    TalosSimulationDialect::ConsequentEndOfSimulationStrengthAttrHelper;
using ConsequentFirstMatchEquivalence =
    TalosSimulationDialect::ConsequentFirstMatchEquivalenceAttrHelper;
using CoverSequencePerMatch =
    TalosSimulationDialect::CoverSequencePerMatchAttrHelper;
using AssertionCoverageReport =
    TalosSimulationDialect::AssertionCoverageReportAttrHelper;
using DefaultAssertionFailure =
    TalosSimulationDialect::DefaultAssertionFailureAttrHelper;
using DeferredEpochStorage =
    TalosSimulationDialect::DeferredEpochStorageAttrHelper;
using DeferredEvaluator = TalosSimulationDialect::DeferredEvaluatorAttrHelper;
using DeferredResult = TalosSimulationDialect::DeferredResultAttrHelper;
using EmptyAntecedentNonoverlap =
    TalosSimulationDialect::EmptyAntecedentNonoverlapAttrHelper;
using EndOfSimulationStrength =
    TalosSimulationDialect::EndOfSimulationStrengthAttrHelper;
using ExpectBoundedAlternatives =
    TalosSimulationDialect::ExpectBoundedAlternativesAttrHelper;
using ExpectBoundedBranching =
    TalosSimulationDialect::ExpectBoundedBranchingAttrHelper;
using ExpectBoundedHorizon =
    TalosSimulationDialect::ExpectBoundedHorizonAttrHelper;
using ExpectBoundedStateWords =
    TalosSimulationDialect::ExpectBoundedStateWordsAttrHelper;
using ExpectDonePath = TalosSimulationDialect::ExpectDonePathAttrHelper;
using ExpectEosCoordinator =
    TalosSimulationDialect::ExpectEosCoordinatorAttrHelper;
using ExpectMonitor = TalosSimulationDialect::ExpectMonitorAttrHelper;
using ExpectMonitorActor =
    TalosSimulationDialect::ExpectMonitorActorAttrHelper;
using ExpectOneCycleAlternatives =
    TalosSimulationDialect::ExpectOneCycleAlternativesAttrHelper;
using ExpectOneCycleBranching =
    TalosSimulationDialect::ExpectOneCycleBranchingAttrHelper;
using ExpectOperandStrength =
    TalosSimulationDialect::ExpectOperandStrengthAttrHelper;
using ExpectResultPath = TalosSimulationDialect::ExpectResultPathAttrHelper;
using FirstMatchMonitor = TalosSimulationDialect::FirstMatchMonitorAttrHelper;
using FirstMatchPriority =
    TalosSimulationDialect::FirstMatchPriorityAttrHelper;
using FirstMatchPriorityGroups =
    TalosSimulationDialect::FirstMatchPriorityGroupsAttrHelper;
using FollowedByMonitor = TalosSimulationDialect::FollowedByMonitorAttrHelper;
using GlobalFutureMonitor =
    TalosSimulationDialect::GlobalFutureMonitorAttrHelper;
using GlobalFutureResolver =
    TalosSimulationDialect::GlobalFutureResolverAttrHelper;
using GlobalFutureWait = TalosSimulationDialect::GlobalFutureWaitAttrHelper;
using ImplicationAntecedent =
    TalosSimulationDialect::ImplicationAntecedentAttrHelper;
using ImplicationAntecedentFailure =
    TalosSimulationDialect::ImplicationAntecedentFailureAttrHelper;
using MixedEmptyAntecedentCurrentTickChannels =
    TalosSimulationDialect::MixedEmptyAntecedentCurrentTickChannelsAttrHelper;
using MixedEmptyAntecedentHandoffChannels =
    TalosSimulationDialect::MixedEmptyAntecedentHandoffChannelsAttrHelper;
using MixedEmptyAntecedentNonoverlap =
    TalosSimulationDialect::MixedEmptyAntecedentNonoverlapAttrHelper;
using MonitorCallback = TalosSimulationDialect::MonitorCallbackAttrHelper;
using MonitorObservationComplete =
    TalosSimulationDialect::MonitorObservationCompleteAttrHelper;
using MulticlockSequenceCoordinator =
    TalosSimulationDialect::MulticlockSequenceCoordinatorAttrHelper;
using MulticlockSequenceEosCoordinator =
    TalosSimulationDialect::MulticlockSequenceEosCoordinatorAttrHelper;
using NegatedOperandEndOfSimulationStrength =
    TalosSimulationDialect::NegatedOperandEndOfSimulationStrengthAttrHelper;
using OverlappedEmptyMatchesIgnored =
    TalosSimulationDialect::OverlappedEmptyMatchesIgnoredAttrHelper;
using PersistentAntecedentFinalHandoff =
    TalosSimulationDialect::PersistentAntecedentFinalHandoffAttrHelper;
using PersistentAntecedentHandoff =
    TalosSimulationDialect::PersistentAntecedentHandoffAttrHelper;
using PersistentAntecedentHandoffStates =
    TalosSimulationDialect::PersistentAntecedentHandoffStatesAttrHelper;
using PersistentDelayAggregateTokens =
    TalosSimulationDialect::PersistentDelayAggregateTokensAttrHelper;
using PersistentDelayAllMatches =
    TalosSimulationDialect::PersistentDelayAllMatchesAttrHelper;
using PersistentDelayImplication =
    TalosSimulationDialect::PersistentDelayImplicationAttrHelper;
using PersistentDelayMinimum =
    TalosSimulationDialect::PersistentDelayMinimumAttrHelper;
using PersistentDelayMonitor =
    TalosSimulationDialect::PersistentDelayMonitorAttrHelper;
using PersistentDelayNonoverlapped =
    TalosSimulationDialect::PersistentDelayNonoverlappedAttrHelper;
using PersistentDelayPrefixHorizon =
    TalosSimulationDialect::PersistentDelayPrefixHorizonAttrHelper;
using PersistentFirstMatchEquivalence =
    TalosSimulationDialect::PersistentFirstMatchEquivalenceAttrHelper;
using PersistentImplicationActivation =
    TalosSimulationDialect::PersistentImplicationActivationAttrHelper;
using PersistentImplicationAntecedentResult =
    TalosSimulationDialect::PersistentImplicationAntecedentResultAttrHelper;
using PersistentImplicationHandoff =
    TalosSimulationDialect::PersistentImplicationHandoffAttrHelper;
using PersistentMonitor = TalosSimulationDialect::PersistentMonitorAttrHelper;
using PersistentRepetitionAllMatches =
    TalosSimulationDialect::PersistentRepetitionAllMatchesAttrHelper;
using PersistentRepetitionAntecedent =
    TalosSimulationDialect::PersistentRepetitionAntecedentAttrHelper;
using PersistentRepetitionDfa =
    TalosSimulationDialect::PersistentRepetitionDfaAttrHelper;
using PersistentRepetitionImplication =
    TalosSimulationDialect::PersistentRepetitionImplicationAttrHelper;
using PersistentRepetitionKind =
    TalosSimulationDialect::PersistentRepetitionKindAttrHelper;
using PersistentRepetitionMax =
    TalosSimulationDialect::PersistentRepetitionMaxAttrHelper;
using PersistentRepetitionMin =
    TalosSimulationDialect::PersistentRepetitionMinAttrHelper;
using PersistentRepetitionMonitor =
    TalosSimulationDialect::PersistentRepetitionMonitorAttrHelper;
using PersistentRepetitionNonoverlapped =
    TalosSimulationDialect::PersistentRepetitionNonoverlappedAttrHelper;
using PersistentRepetitionStates =
    TalosSimulationDialect::PersistentRepetitionStatesAttrHelper;
using PersistentRepetitionUnbounded =
    TalosSimulationDialect::PersistentRepetitionUnboundedAttrHelper;
using PersistentUnaryAggregateTokens =
    TalosSimulationDialect::PersistentUnaryAggregateTokensAttrHelper;
using PersistentUnaryImplication =
    TalosSimulationDialect::PersistentUnaryImplicationAttrHelper;
using PersistentUnaryKind =
    TalosSimulationDialect::PersistentUnaryKindAttrHelper;
using PersistentUnaryMinimum =
    TalosSimulationDialect::PersistentUnaryMinimumAttrHelper;
using PersistentUnaryMonitor =
    TalosSimulationDialect::PersistentUnaryMonitorAttrHelper;
using PersistentUnaryNonoverlapped =
    TalosSimulationDialect::PersistentUnaryNonoverlappedAttrHelper;
using PersistentUntilAggregateTokens =
    TalosSimulationDialect::PersistentUntilAggregateTokensAttrHelper;
using PersistentUntilImplication =
    TalosSimulationDialect::PersistentUntilImplicationAttrHelper;
using PersistentUntilInclusive =
    TalosSimulationDialect::PersistentUntilInclusiveAttrHelper;
using PersistentUntilKind =
    TalosSimulationDialect::PersistentUntilKindAttrHelper;
using PersistentUntilMonitor =
    TalosSimulationDialect::PersistentUntilMonitorAttrHelper;
using PersistentUntilNonoverlapped =
    TalosSimulationDialect::PersistentUntilNonoverlappedAttrHelper;
using PersistentUntilStrong =
    TalosSimulationDialect::PersistentUntilStrongAttrHelper;
using ProceduralAssertionAttempt =
    TalosSimulationDialect::ProceduralAssertionAttemptAttrHelper;
using ProceduralAssertionEpochReference =
    TalosSimulationDialect::ProceduralAssertionEpochReferenceAttrHelper;
using ProceduralAssertionEpochValue =
    TalosSimulationDialect::ProceduralAssertionEpochValueAttrHelper;
using PropertyAbortAction =
    TalosSimulationDialect::PropertyAbortActionAttrHelper;
using SampleDefaultConstant =
    TalosSimulationDialect::SampleDefaultConstantAttrHelper;
using SampleDefaultCurrent =
    TalosSimulationDialect::SampleDefaultCurrentAttrHelper;
using SampleDefaultDynamic =
    TalosSimulationDialect::SampleDefaultDynamicAttrHelper;
using SampleDefaultSnapshot =
    TalosSimulationDialect::SampleDefaultSnapshotAttrHelper;
using SequenceEndpointDefaultClock =
    TalosSimulationDialect::SequenceEndpointDefaultClockAttrHelper;
using SequenceEndpointEvent =
    TalosSimulationDialect::SequenceEndpointEventAttrHelper;
using SequenceEndpointMonitor =
    TalosSimulationDialect::SequenceEndpointMonitorAttrHelper;
using SequenceEndpointPath =
    TalosSimulationDialect::SequenceEndpointPathAttrHelper;
using StrongWeakMonitor = TalosSimulationDialect::StrongWeakMonitorAttrHelper;
using SvaTransitionNormalForm =
    TalosSimulationDialect::SvaTransitionNormalFormAttrHelper;
using SynchronousPropertyAbort =
    TalosSimulationDialect::SynchronousPropertyAbortAttrHelper;
using TemporalPropertyNegation =
    TalosSimulationDialect::TemporalPropertyNegationAttrHelper;
using TemporalPropertyNegationOutsideAbort =
    TalosSimulationDialect::TemporalPropertyNegationOutsideAbortAttrHelper;
using VacuousSequenceAlternatives =
    TalosSimulationDialect::VacuousSequenceAlternativesAttrHelper;
using SteadyUnknown = TalosSimulationDialect::SteadyUnknownAttrHelper;
using ContinuousStore = TalosSimulationDialect::ContinuousStoreAttrHelper;
using DelayQuantum = TalosSimulationDialect::DelayQuantumAttrHelper;
using DelayScale = TalosSimulationDialect::DelayScaleAttrHelper;
using DelayedNet = TalosSimulationDialect::DelayedNetAttrHelper;
using InitialDriverX = TalosSimulationDialect::InitialDriverXAttrHelper;
using InterconnectLeafPath =
    TalosSimulationDialect::InterconnectLeafPathAttrHelper;
using InterconnectLeaves =
    TalosSimulationDialect::InterconnectLeavesAttrHelper;
using InterconnectNettype =
    TalosSimulationDialect::InterconnectNettypeAttrHelper;
using InterconnectVpiType =
    TalosSimulationDialect::InterconnectVpiTypeAttrHelper;
using MosTopologyIds = TalosSimulationDialect::MosTopologyIdsAttrHelper;
using NegativeTimingAdjusted =
    TalosSimulationDialect::NegativeTimingAdjustedAttrHelper;
using NegativeTimingDelayCommit =
    TalosSimulationDialect::NegativeTimingDelayCommitAttrHelper;
using NegativeTimingDelayMonitor =
    TalosSimulationDialect::NegativeTimingDelayMonitorAttrHelper;
using NegativeTimingTransportActivation =
    TalosSimulationDialect::NegativeTimingTransportActivationAttrHelper;
using PassSwitchIds = TalosSimulationDialect::PassSwitchIdsAttrHelper;
using PropagationDelays = TalosSimulationDialect::PropagationDelaysAttrHelper;
using ResolutionFunctionPath =
    TalosSimulationDialect::ResolutionFunctionPathAttrHelper;
using ResolutionFunctionSymbol =
    TalosSimulationDialect::ResolutionFunctionSymbolAttrHelper;
using SkewDeadlineHelper =
    TalosSimulationDialect::SkewDeadlineHelperAttrHelper;
using StrengthBank = TalosSimulationDialect::StrengthBankAttrHelper;
using StrengthDriverBank =
    TalosSimulationDialect::StrengthDriverBankAttrHelper;
using StrengthGroup = TalosSimulationDialect::StrengthGroupAttrHelper;
using TimingAdjustedTicks =
    TalosSimulationDialect::TimingAdjustedTicksAttrHelper;
using TimingCheckArgTicks =
    TalosSimulationDialect::TimingCheckArgTicksAttrHelper;
using TimingCheckCoordinator =
    TalosSimulationDialect::TimingCheckCoordinatorAttrHelper;
using TimingDelayedSourceDelays =
    TalosSimulationDialect::TimingDelayedSourceDelaysAttrHelper;
using TimingDelayedStorageIds =
    TalosSimulationDialect::TimingDelayedStorageIdsAttrHelper;
using TimingDelayedTerminalIndices =
    TalosSimulationDialect::TimingDelayedTerminalIndicesAttrHelper;
using TimingPathMonitorRules =
    TalosSimulationDialect::TimingPathMonitorRulesAttrHelper;
using TimingPathRules = TalosSimulationDialect::TimingPathRulesAttrHelper;
using TimingTimerHelper = TalosSimulationDialect::TimingTimerHelperAttrHelper;
using TimingTimerStorage =
    TalosSimulationDialect::TimingTimerStorageAttrHelper;
using UdpMetadata = TalosSimulationDialect::UdpMetadataAttrHelper;
using UserDefinedNet = TalosSimulationDialect::UserDefinedNetAttrHelper;
using UserNetDriver = TalosSimulationDialect::UserNetDriverAttrHelper;
using UserNetRawDrive = TalosSimulationDialect::UserNetRawDriveAttrHelper;
using AggregateMemberInitializers =
    TalosSimulationDialect::AggregateMemberInitializersAttrHelper;
using AutomaticReferenceCapture =
    TalosSimulationDialect::AutomaticReferenceCaptureAttrHelper;
using Bindings = TalosSimulationDialect::BindingsAttrHelper;
using Callee = TalosSimulationDialect::CalleeAttrHelper;
using CalleeCaptures = TalosSimulationDialect::CalleeCapturesAttrHelper;
using CalleeFormals = TalosSimulationDialect::CalleeFormalsAttrHelper;
using CalleeReadCaptures =
    TalosSimulationDialect::CalleeReadCapturesAttrHelper;
using CalleeWrittenCaptures =
    TalosSimulationDialect::CalleeWrittenCapturesAttrHelper;
using CaptureKind = TalosSimulationDialect::CaptureKindAttrHelper;
using ClassBitstreamAllowHiddenRoot =
    TalosSimulationDialect::ClassBitstreamAllowHiddenRootAttrHelper;
using ClassBitstreamBytecodeFunction =
    TalosSimulationDialect::ClassBitstreamBytecodeFunctionAttrHelper;
using ClassBitstreamBytecodeSite =
    TalosSimulationDialect::ClassBitstreamBytecodeSiteAttrHelper;
using ClassBitstreamMember =
    TalosSimulationDialect::ClassBitstreamMemberAttrHelper;
using ClassBitstreamSource =
    TalosSimulationDialect::ClassBitstreamSourceAttrHelper;
using ClassBitstreamVisibility =
    TalosSimulationDialect::ClassBitstreamVisibilityAttrHelper;
using ClassBitstreamWidth =
    TalosSimulationDialect::ClassBitstreamWidthAttrHelper;
using ClassField = TalosSimulationDialect::ClassFieldAttrHelper;
using ClassInstance = TalosSimulationDialect::ClassInstanceAttrHelper;
using ClassMethod = TalosSimulationDialect::ClassMethodAttrHelper;
using ClassSignature = TalosSimulationDialect::ClassSignatureAttrHelper;
using ClassSlot = TalosSimulationDialect::ClassSlotAttrHelper;
using ClassSuper = TalosSimulationDialect::ClassSuperAttrHelper;
using ClassVirtual = TalosSimulationDialect::ClassVirtualAttrHelper;
using ConstantValue = TalosSimulationDialect::ConstantValueAttrHelper;
using Constructor = TalosSimulationDialect::ConstructorAttrHelper;
using ControlTargetId = TalosSimulationDialect::ControlTargetIdAttrHelper;
using CovergroupBlockEventSampleEvaluator =
    TalosSimulationDialect::CovergroupBlockEventSampleEvaluatorAttrHelper;
using CovergroupEventSampleEvaluator =
    TalosSimulationDialect::CovergroupEventSampleEvaluatorAttrHelper;
using CovergroupStrobeSampleEvaluator =
    TalosSimulationDialect::CovergroupStrobeSampleEvaluatorAttrHelper;
using EventExplicitInitializer =
    TalosSimulationDialect::EventExplicitInitializerAttrHelper;
using ForeachIteratorObserverCapture =
    TalosSimulationDialect::ForeachIteratorObserverCaptureAttrHelper;
using ForkCodeUnitId = TalosSimulationDialect::ForkCodeUnitIdAttrHelper;
using InitializeField = TalosSimulationDialect::InitializeFieldAttrHelper;
using InitializeNet = TalosSimulationDialect::InitializeNetAttrHelper;
using InitializeSampleDefault =
    TalosSimulationDialect::InitializeSampleDefaultAttrHelper;
using InitializeStatic = TalosSimulationDialect::InitializeStaticAttrHelper;
using InitializeSubelement =
    TalosSimulationDialect::InitializeSubelementAttrHelper;
using InitializeTypeOptionKind =
    TalosSimulationDialect::InitializeTypeOptionKindAttrHelper;
using InitializeTypeOptionOwner =
    TalosSimulationDialect::InitializeTypeOptionOwnerAttrHelper;
using IsTask = TalosSimulationDialect::IsTaskAttrHelper;
using Lowered = TalosSimulationDialect::LoweredAttrHelper;
using NbaEventCodeUnitId =
    TalosSimulationDialect::NbaEventCodeUnitIdAttrHelper;
using NbaEventHierarchy = TalosSimulationDialect::NbaEventHierarchyAttrHelper;
using Observer = TalosSimulationDialect::ObserverAttrHelper;
using ObserverCaptures = TalosSimulationDialect::ObserverCapturesAttrHelper;
using ObserverCoercedType =
    TalosSimulationDialect::ObserverCoercedTypeAttrHelper;
using ObserverDependencies =
    TalosSimulationDialect::ObserverDependenciesAttrHelper;
using ObserverResult = TalosSimulationDialect::ObserverResultAttrHelper;
using Placeholder = TalosSimulationDialect::PlaceholderAttrHelper;
using PreparedInitializer =
    TalosSimulationDialect::PreparedInitializerAttrHelper;
using ResumableControlTarget =
    TalosSimulationDialect::ResumableControlTargetAttrHelper;
using ExternalControlTarget =
    TalosSimulationDialect::ExternalControlTargetAttrHelper;
using ReturnVariablePath =
    TalosSimulationDialect::ReturnVariablePathAttrHelper;
using SampledObserver = TalosSimulationDialect::SampledObserverAttrHelper;
using StaticClassProperty =
    TalosSimulationDialect::StaticClassPropertyAttrHelper;
using StaticClassReceiver =
    TalosSimulationDialect::StaticClassReceiverAttrHelper;
using StaticInitializer = TalosSimulationDialect::StaticInitializerAttrHelper;
using StaticNetConstant = TalosSimulationDialect::StaticNetConstantAttrHelper;
using StaticSiteId = TalosSimulationDialect::StaticSiteIdAttrHelper;
using SubroutineStorage = TalosSimulationDialect::SubroutineStorageAttrHelper;
using ThisArgument = TalosSimulationDialect::ThisArgumentAttrHelper;
using TypeOptionItemQualifier =
    TalosSimulationDialect::TypeOptionItemQualifierAttrHelper;
using VirtualInterfaceCallees =
    TalosSimulationDialect::VirtualInterfaceCalleesAttrHelper;
using VirtualInterfaceClockEvents =
    TalosSimulationDialect::VirtualInterfaceClockEventsAttrHelper;
using VirtualInterfaceClockingOutput =
    TalosSimulationDialect::VirtualInterfaceClockingOutputAttrHelper;
using VirtualInterfaceMember =
    TalosSimulationDialect::VirtualInterfaceMemberAttrHelper;
using VirtualInterfaceParentMember =
    TalosSimulationDialect::VirtualInterfaceParentMemberAttrHelper;
using VirtualInterfaceReceiverMembers =
    TalosSimulationDialect::VirtualInterfaceReceiverMembersAttrHelper;
using VoidFunction = TalosSimulationDialect::VoidFunctionAttrHelper;
using ActivationDefaultSlot =
    TalosSimulationDialect::ActivationDefaultSlotAttrHelper;
using BytecodeReadOnly = TalosSimulationDialect::BytecodeReadOnlyAttrHelper;
using DescriptorAggregateType =
    TalosSimulationDialect::DescriptorAggregateTypeAttrHelper;
using DescriptorId = TalosSimulationDialect::DescriptorIdAttrHelper;
using DescriptorIndices = TalosSimulationDialect::DescriptorIndicesAttrHelper;
using DescriptorLow = TalosSimulationDialect::DescriptorLowAttrHelper;
using DescriptorPackedLow =
    TalosSimulationDialect::DescriptorPackedLowAttrHelper;
using DescriptorRootType =
    TalosSimulationDialect::DescriptorRootTypeAttrHelper;
using FlushedPrivateSlot =
    TalosSimulationDialect::FlushedPrivateSlotAttrHelper;
using GcPollFree = TalosSimulationDialect::GcPollFreeAttrHelper;
using HierarchicalName = TalosSimulationDialect::HierarchicalNameAttrHelper;
using Rematerialized = TalosSimulationDialect::RematerializedAttrHelper;
using VpiAnchor = TalosSimulationDialect::VpiAnchorAttrHelper;
using VpiDefinitionMember =
    TalosSimulationDialect::VpiDefinitionMemberAttrHelper;
using VpiExpressionIdentifier =
    TalosSimulationDialect::VpiExpressionIdentifierAttrHelper;
using VpiStatementId = TalosSimulationDialect::VpiStatementIdAttrHelper;
using VpiCallSite = TalosSimulationDialect::VpiCallSiteAttrHelper;
using VpiResolutionNet = TalosSimulationDialect::VpiResolutionNetAttrHelper;
using VpiLocal = TalosSimulationDialect::VpiLocalAttrHelper;
using VpiDeclaredType = TalosSimulationDialect::VpiDeclaredTypeAttrHelper;
using VpiFrameInstrumentation =
    TalosSimulationDialect::VpiFrameInstrumentationAttrHelper;
using VpiAutomatic = TalosSimulationDialect::VpiAutomaticAttrHelper;
using VpiCellInstance = TalosSimulationDialect::VpiCellInstanceAttrHelper;
using VpiDefinitionName = TalosSimulationDialect::VpiDefinitionNameAttrHelper;
using VpiIdentityDelegated =
    TalosSimulationDialect::VpiIdentityDelegatedAttrHelper;
using VpiBoxType = TalosSimulationDialect::VpiBoxTypeAttrHelper;
using VpiSourceTypeIdentity =
    TalosSimulationDialect::VpiSourceTypeIdentityAttrHelper;
using VpiTop = TalosSimulationDialect::VpiTopAttrHelper;
using DumpportsScope = TalosSimulationDialect::DumpportsScopeAttrHelper;
using DynamicCastEnumValues =
    TalosSimulationDialect::DynamicCastEnumValuesAttrHelper;
using DynamicCastTask = TalosSimulationDialect::DynamicCastTaskAttrHelper;
using EnumFormatNames = TalosSimulationDialect::EnumFormatNamesAttrHelper;
using EnumFormatValues = TalosSimulationDialect::EnumFormatValuesAttrHelper;
using EnumFormats = TalosSimulationDialect::EnumFormatsAttrHelper;
using EnumMethodNames = TalosSimulationDialect::EnumMethodNamesAttrHelper;
using EnumMethodValues = TalosSimulationDialect::EnumMethodValuesAttrHelper;
using BooleanMinimizations =
    TalosSimulationDialect::BooleanMinimizationsAttrHelper;

} // namespace talos::sim::metadata

namespace talos::sim {

using discardable::get;
using discardable::has;
using discardable::mark;
using discardable::named;
using discardable::remove;
using discardable::set;

} // namespace talos::sim

#endif // TALOS_DIALECT_SIMULATION_SIMULATIONDIALECT_H
