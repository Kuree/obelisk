//===- SimulationDialect.h - Executable simulation dialect -----*- C++ -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONDIALECT_H
#define OBELISK_DIALECT_SIMULATION_SIMULATIONDIALECT_H

#include "obelisk/Dialect/DiscardableAttributes.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Dialect.h"

// The dialect's attribute helpers name its attribute and type classes.
#include "obelisk/Dialect/Simulation/SimulationAttrs.h"

#include "obelisk/Dialect/Simulation/SimulationDialect.h.inc"

/// Typed handles of the declared `simulation.*` attributes, for use with
/// `sim::get`, `set`, `has`, `mark`, `remove` and `named`.
namespace obelisk::sim::metadata {

using IntegerRange = ObeliskSimulationDialect::IntegerRangeAttrHelper;
using ResultRanges = ObeliskSimulationDialect::ResultRangesAttrHelper;
using ResultExprs = ObeliskSimulationDialect::ResultExprsAttrHelper;
using KnownLogicResult = ObeliskSimulationDialect::KnownLogicResultAttrHelper;
using GCPolled = ObeliskSimulationDialect::GcPolledAttrHelper;
using InlineLegalityCache = ObeliskSimulationDialect::InlineLegalityAttrHelper;
using PulseRejectPercent =
    ObeliskSimulationDialect::PulseRejectPercentAttrHelper;
using PulseErrorPercent = ObeliskSimulationDialect::PulseErrorPercentAttrHelper;
using PulseOnDetect = ObeliskSimulationDialect::PulseOnDetectAttrHelper;
using PulseShowCancelled =
    ObeliskSimulationDialect::PulseShowCancelledAttrHelper;
using ConstraintMode = ObeliskSimulationDialect::ConstraintModeAttrHelper;
using ConstraintModeBlock =
    ObeliskSimulationDialect::ConstraintModeBlockAttrHelper;
using ConstraintModeField =
    ObeliskSimulationDialect::ConstraintModeFieldAttrHelper;
using ConstraintModeStaticStorage =
    ObeliskSimulationDialect::ConstraintModeStaticStorageAttrHelper;
using ConstraintModeStaticStorages =
    ObeliskSimulationDialect::ConstraintModeStaticStoragesAttrHelper;
using ObjectRandomDispatchClasses =
    ObeliskSimulationDialect::ObjectRandomDispatchClassesAttrHelper;
using RandMode = ObeliskSimulationDialect::RandModeAttrHelper;
using RandModeAncestorMask =
    ObeliskSimulationDialect::RandModeAncestorMaskAttrHelper;
using RandModeArrayField =
    ObeliskSimulationDialect::RandModeArrayFieldAttrHelper;
using RandModeArrayStorage =
    ObeliskSimulationDialect::RandModeArrayStorageAttrHelper;
using RandModeArrayType = ObeliskSimulationDialect::RandModeArrayTypeAttrHelper;
using RandModeFamilyMask =
    ObeliskSimulationDialect::RandModeFamilyMaskAttrHelper;
using RandModeProperty = ObeliskSimulationDialect::RandModePropertyAttrHelper;
using RandModeSelectionMask =
    ObeliskSimulationDialect::RandModeSelectionMaskAttrHelper;
using RandModeStaticDispatch =
    ObeliskSimulationDialect::RandModeStaticDispatchAttrHelper;
using RandModeStaticStorage =
    ObeliskSimulationDialect::RandModeStaticStorageAttrHelper;
using RandModeStructDepth =
    ObeliskSimulationDialect::RandModeStructDepthAttrHelper;
using RandModeStructElementFields =
    ObeliskSimulationDialect::RandModeStructElementFieldsAttrHelper;
using RandomArithmeticNode =
    ObeliskSimulationDialect::RandomArithmeticNodeAttrHelper;
using RandomArithmeticOperand =
    ObeliskSimulationDialect::RandomArithmeticOperandAttrHelper;
using RandomArithmeticPlane =
    ObeliskSimulationDialect::RandomArithmeticPlaneAttrHelper;
using RandomArrayElementWidth =
    ObeliskSimulationDialect::RandomArrayElementWidthAttrHelper;
using RandomArrayModeField =
    ObeliskSimulationDialect::RandomArrayModeFieldAttrHelper;
using RandomConstraintBlock =
    ObeliskSimulationDialect::RandomConstraintBlockAttrHelper;
using RandomConstraintCount =
    ObeliskSimulationDialect::RandomConstraintCountAttrHelper;
using RandomContainerProperties =
    ObeliskSimulationDialect::RandomContainerPropertiesAttrHelper;
using RandomCycleKeyField =
    ObeliskSimulationDialect::RandomCycleKeyFieldAttrHelper;
using RandomCyclePositionField =
    ObeliskSimulationDialect::RandomCyclePositionFieldAttrHelper;
using RandomEvaluationError =
    ObeliskSimulationDialect::RandomEvaluationErrorAttrHelper;
using RandomEvaluationInputs =
    ObeliskSimulationDialect::RandomEvaluationInputsAttrHelper;
using RandomFixedConstraintModes =
    ObeliskSimulationDialect::RandomFixedConstraintModesAttrHelper;
using RandomFunctionCaseCondition =
    ObeliskSimulationDialect::RandomFunctionCaseConditionAttrHelper;
using RandomFunctionConditional =
    ObeliskSimulationDialect::RandomFunctionConditionalAttrHelper;
using RandomFunctionExpression =
    ObeliskSimulationDialect::RandomFunctionExpressionAttrHelper;
using RandomFunctionOrder =
    ObeliskSimulationDialect::RandomFunctionOrderAttrHelper;
using RandomFunctionState =
    ObeliskSimulationDialect::RandomFunctionStateAttrHelper;
using RandomGuardInputs = ObeliskSimulationDialect::RandomGuardInputsAttrHelper;
using RandomIncrementField =
    ObeliskSimulationDialect::RandomIncrementFieldAttrHelper;
using RandomModeField = ObeliskSimulationDialect::RandomModeFieldAttrHelper;
using RandomModeIndex = ObeliskSimulationDialect::RandomModeIndexAttrHelper;
using RandomNestedConstraintModes =
    ObeliskSimulationDialect::RandomNestedConstraintModesAttrHelper;
using RandomNestedHooks = ObeliskSimulationDialect::RandomNestedHooksAttrHelper;
using RandomNestedStateConcreteType =
    ObeliskSimulationDialect::RandomNestedStateConcreteTypeAttrHelper;
using RandomNestedStateField =
    ObeliskSimulationDialect::RandomNestedStateFieldAttrHelper;
using RandomNestedStatePath =
    ObeliskSimulationDialect::RandomNestedStatePathAttrHelper;
using RandomNestedStateStorageType =
    ObeliskSimulationDialect::RandomNestedStateStorageTypeAttrHelper;
using RandomObjectEdge = ObeliskSimulationDialect::RandomObjectEdgeAttrHelper;
using EmbeddedCovergroup =
    ObeliskSimulationDialect::EmbeddedCovergroupAttrHelper;
using CheckerProcedure = ObeliskSimulationDialect::CheckerProcedureAttrHelper;
using RandomPostHook = ObeliskSimulationDialect::RandomPostHookAttrHelper;
using RandomPreHook = ObeliskSimulationDialect::RandomPreHookAttrHelper;
using RandomPriorityArguments =
    ObeliskSimulationDialect::RandomPriorityArgumentsAttrHelper;
using RandomPriorityInputs =
    ObeliskSimulationDialect::RandomPriorityInputsAttrHelper;
using RandomProperties = ObeliskSimulationDialect::RandomPropertiesAttrHelper;
using RandomRealFinite = ObeliskSimulationDialect::RandomRealFiniteAttrHelper;
using RandomReceiverIndex =
    ObeliskSimulationDialect::RandomReceiverIndexAttrHelper;
using RandomRecursiveAliasGuards =
    ObeliskSimulationDialect::RandomRecursiveAliasGuardsAttrHelper;
using RandomStateField = ObeliskSimulationDialect::RandomStateFieldAttrHelper;
using RandomStructMembers =
    ObeliskSimulationDialect::RandomStructMembersAttrHelper;
using RandomTotalWidth = ObeliskSimulationDialect::RandomTotalWidthAttrHelper;
using RandomVariable = ObeliskSimulationDialect::RandomVariableAttrHelper;
using RandomVariableBitOffset =
    ObeliskSimulationDialect::RandomVariableBitOffsetAttrHelper;
using RandomVariableKind =
    ObeliskSimulationDialect::RandomVariableKindAttrHelper;
using RandomVariableSigned =
    ObeliskSimulationDialect::RandomVariableSignedAttrHelper;
using Randomize = ObeliskSimulationDialect::RandomizeAttrHelper;
using RandomizeCheckerOnly =
    ObeliskSimulationDialect::RandomizeCheckerOnlyAttrHelper;
using RandomizeConstraintTemplate =
    ObeliskSimulationDialect::RandomizeConstraintTemplateAttrHelper;
using RandomizeDispatch = ObeliskSimulationDialect::RandomizeDispatchAttrHelper;
using RandomizeExplicitProperties =
    ObeliskSimulationDialect::RandomizeExplicitPropertiesAttrHelper;
using RandomizeHelper = ObeliskSimulationDialect::RandomizeHelperAttrHelper;
using RandomizeHelperCaptures =
    ObeliskSimulationDialect::RandomizeHelperCapturesAttrHelper;
using RandomizeHelperReadCaptures =
    ObeliskSimulationDialect::RandomizeHelperReadCapturesAttrHelper;
using RandomizeHelperReceiver =
    ObeliskSimulationDialect::RandomizeHelperReceiverAttrHelper;
using RandomizeNestedDispatch =
    ObeliskSimulationDialect::RandomizeNestedDispatchAttrHelper;
using RandomizeNestedDispatchField =
    ObeliskSimulationDialect::RandomizeNestedDispatchFieldAttrHelper;
using RandomizeNestedDispatchPath =
    ObeliskSimulationDialect::RandomizeNestedDispatchPathAttrHelper;
using RandomizeNestedDispatchSelectionPath =
    ObeliskSimulationDialect::RandomizeNestedDispatchSelectionPathAttrHelper;
using RandomizeNestedDispatchStorageType =
    ObeliskSimulationDialect::RandomizeNestedDispatchStorageTypeAttrHelper;
using RandomizeNestedPlans =
    ObeliskSimulationDialect::RandomizeNestedPlansAttrHelper;
using RandomizePlanClass =
    ObeliskSimulationDialect::RandomizePlanClassAttrHelper;
using RandomizeScope = ObeliskSimulationDialect::RandomizeScopeAttrHelper;
using StaticConstraintStorage =
    ObeliskSimulationDialect::StaticConstraintStorageAttrHelper;
using StaticRandomArrayModeStorage =
    ObeliskSimulationDialect::StaticRandomArrayModeStorageAttrHelper;
using StaticRandomModeStorage =
    ObeliskSimulationDialect::StaticRandomModeStorageAttrHelper;
using AssertionActionControlled =
    ObeliskSimulationDialect::AssertionActionControlledAttrHelper;
using AssertionControlAction =
    ObeliskSimulationDialect::AssertionControlActionAttrHelper;
using AssertionControlActionArgument =
    ObeliskSimulationDialect::AssertionControlActionArgumentAttrHelper;
using AssertionControlAssertionTypes =
    ObeliskSimulationDialect::AssertionControlAssertionTypesAttrHelper;
using AssertionControlAssertionTypesArgument =
    ObeliskSimulationDialect::AssertionControlAssertionTypesArgumentAttrHelper;
using AssertionControlDepths =
    ObeliskSimulationDialect::AssertionControlDepthsAttrHelper;
using AssertionControlDirectiveTypes =
    ObeliskSimulationDialect::AssertionControlDirectiveTypesAttrHelper;
using AssertionControlDirectiveTypesArgument =
    ObeliskSimulationDialect::AssertionControlDirectiveTypesArgumentAttrHelper;
using AssertionControlIds =
    ObeliskSimulationDialect::AssertionControlIdsAttrHelper;
using AssertionControlLevelsArgument =
    ObeliskSimulationDialect::AssertionControlLevelsArgumentAttrHelper;
using AssertionControlTargetId =
    ObeliskSimulationDialect::AssertionControlTargetIdAttrHelper;
using AssertionControlled =
    ObeliskSimulationDialect::AssertionControlledAttrHelper;
using AssertionKillControlled =
    ObeliskSimulationDialect::AssertionKillControlledAttrHelper;
using AssertionLocalTypes =
    ObeliskSimulationDialect::AssertionLocalTypesAttrHelper;
using AssertionPath = ObeliskSimulationDialect::AssertionPathAttrHelper;
using AssertionTargetId = ObeliskSimulationDialect::AssertionTargetIdAttrHelper;
using AsynchronousPropertyAbort =
    ObeliskSimulationDialect::AsynchronousPropertyAbortAttrHelper;
using BoundedAntecedentHorizon =
    ObeliskSimulationDialect::BoundedAntecedentHorizonAttrHelper;
using BranchingAntecedentAlternatives =
    ObeliskSimulationDialect::BranchingAntecedentAlternativesAttrHelper;
using BranchingAntecedentBackedge =
    ObeliskSimulationDialect::BranchingAntecedentBackedgeAttrHelper;
using BranchingAntecedentChannel =
    ObeliskSimulationDialect::BranchingAntecedentChannelAttrHelper;
using BranchingAntecedentConsequentEosStrength = ObeliskSimulationDialect::
    BranchingAntecedentConsequentEosStrengthAttrHelper;
using BranchingAntecedentConsequentTrigger =
    ObeliskSimulationDialect::BranchingAntecedentConsequentTriggerAttrHelper;
using BranchingAntecedentEosCoalescer =
    ObeliskSimulationDialect::BranchingAntecedentEosCoalescerAttrHelper;
using BranchingAntecedentEosReport =
    ObeliskSimulationDialect::BranchingAntecedentEosReportAttrHelper;
using BranchingAntecedentEosResult =
    ObeliskSimulationDialect::BranchingAntecedentEosResultAttrHelper;
using BranchingAntecedentEosSourceAge =
    ObeliskSimulationDialect::BranchingAntecedentEosSourceAgeAttrHelper;
using BranchingAntecedentExistentialFailure =
    ObeliskSimulationDialect::BranchingAntecedentExistentialFailureAttrHelper;
using BranchingAntecedentExistentialSuccess =
    ObeliskSimulationDialect::BranchingAntecedentExistentialSuccessAttrHelper;
using BranchingAntecedentMatchChannels =
    ObeliskSimulationDialect::BranchingAntecedentMatchChannelsAttrHelper;
using BranchingAntecedentMatchedHistory =
    ObeliskSimulationDialect::BranchingAntecedentMatchedHistoryAttrHelper;
using BranchingAntecedentMonitor =
    ObeliskSimulationDialect::BranchingAntecedentMonitorAttrHelper;
using BranchingAntecedentResultCancel =
    ObeliskSimulationDialect::BranchingAntecedentResultCancelAttrHelper;
using BranchingAntecedentResultCoalescer =
    ObeliskSimulationDialect::BranchingAntecedentResultCoalescerAttrHelper;
using BranchingAntecedentResultHorizon =
    ObeliskSimulationDialect::BranchingAntecedentResultHorizonAttrHelper;
using BranchingAntecedentUniversalFailure =
    ObeliskSimulationDialect::BranchingAntecedentUniversalFailureAttrHelper;
using BranchingAntecedentUniversalSuccess =
    ObeliskSimulationDialect::BranchingAntecedentUniversalSuccessAttrHelper;
using BranchingAntecedentVacuity =
    ObeliskSimulationDialect::BranchingAntecedentVacuityAttrHelper;
using BranchingConsequentAlternative =
    ObeliskSimulationDialect::BranchingConsequentAlternativeAttrHelper;
using BranchingConsequentAlternativeCancel =
    ObeliskSimulationDialect::BranchingConsequentAlternativeCancelAttrHelper;
using BranchingConsequentAlternatives =
    ObeliskSimulationDialect::BranchingConsequentAlternativesAttrHelper;
using BranchingConsequentBackedge =
    ObeliskSimulationDialect::BranchingConsequentBackedgeAttrHelper;
using BranchingConsequentEosCoalescer =
    ObeliskSimulationDialect::BranchingConsequentEosCoalescerAttrHelper;
using BranchingConsequentIntrinsicEosStrength =
    ObeliskSimulationDialect::BranchingConsequentIntrinsicEosStrengthAttrHelper;
using BranchingConsequentMonitor =
    ObeliskSimulationDialect::BranchingConsequentMonitorAttrHelper;
using BranchingConsequentNonoverlapped =
    ObeliskSimulationDialect::BranchingConsequentNonoverlappedAttrHelper;
using BranchingConsequentTrigger =
    ObeliskSimulationDialect::BranchingConsequentTriggerAttrHelper;
using BranchingSequenceAlternatives =
    ObeliskSimulationDialect::BranchingSequenceAlternativesAttrHelper;
using BranchingSequenceMonitor =
    ObeliskSimulationDialect::BranchingSequenceMonitorAttrHelper;
using ClockingEventMonitor =
    ObeliskSimulationDialect::ClockingEventMonitorAttrHelper;
using ClockingEventMonitorPath =
    ObeliskSimulationDialect::ClockingEventMonitorPathAttrHelper;
using CombinedBooleanBranchingMonitor =
    ObeliskSimulationDialect::CombinedBooleanBranchingMonitorAttrHelper;
using CombinedBooleanBranchingPairs =
    ObeliskSimulationDialect::CombinedBooleanBranchingPairsAttrHelper;
using CombinedBooleanBranchingPairsBeforeMinimization =
    ObeliskSimulationDialect::
        CombinedBooleanBranchingPairsBeforeMinimizationAttrHelper;
using CombinedBoundedBranchingMonitor =
    ObeliskSimulationDialect::CombinedBoundedBranchingMonitorAttrHelper;
using CombinedBoundedBranchingPairs =
    ObeliskSimulationDialect::CombinedBoundedBranchingPairsAttrHelper;
using CombinedBoundedBranchingPairsBeforeMinimization =
    ObeliskSimulationDialect::
        CombinedBoundedBranchingPairsBeforeMinimizationAttrHelper;
using ConcurrentAbortCounted =
    ObeliskSimulationDialect::ConcurrentAbortCountedAttrHelper;
using ConcurrentActionClass =
    ObeliskSimulationDialect::ConcurrentActionClassAttrHelper;
using ConcurrentActionControl =
    ObeliskSimulationDialect::ConcurrentActionControlAttrHelper;
using ConcurrentAttemptActionState =
    ObeliskSimulationDialect::ConcurrentAttemptActionStateAttrHelper;
using ConcurrentAttemptActionStateLoad =
    ObeliskSimulationDialect::ConcurrentAttemptActionStateLoadAttrHelper;
using ConcurrentAttemptActionStateNext =
    ObeliskSimulationDialect::ConcurrentAttemptActionStateNextAttrHelper;
using ConcurrentAttemptActionStateStorage =
    ObeliskSimulationDialect::ConcurrentAttemptActionStateStorageAttrHelper;
using ConcurrentAttemptActionStateStore =
    ObeliskSimulationDialect::ConcurrentAttemptActionStateStoreAttrHelper;
using ConcurrentAttemptEnable =
    ObeliskSimulationDialect::ConcurrentAttemptEnableAttrHelper;
using ConcurrentAttemptStart =
    ObeliskSimulationDialect::ConcurrentAttemptStartAttrHelper;
using ConcurrentEosForcedCompletion =
    ObeliskSimulationDialect::ConcurrentEosForcedCompletionAttrHelper;
using ConcurrentEosReport =
    ObeliskSimulationDialect::ConcurrentEosReportAttrHelper;
using ConcurrentEosVacuous =
    ObeliskSimulationDialect::ConcurrentEosVacuousAttrHelper;
using ConcurrentKillEpoch =
    ObeliskSimulationDialect::ConcurrentKillEpochAttrHelper;
using ConcurrentKillEpochCheck =
    ObeliskSimulationDialect::ConcurrentKillEpochCheckAttrHelper;
using ConcurrentKillEpochStorage =
    ObeliskSimulationDialect::ConcurrentKillEpochStorageAttrHelper;
using ConcurrentMatchCall =
    ObeliskSimulationDialect::ConcurrentMatchCallAttrHelper;
using ConcurrentMatchCallChain =
    ObeliskSimulationDialect::ConcurrentMatchCallChainAttrHelper;
using ConcurrentMatchCallKillEpoch =
    ObeliskSimulationDialect::ConcurrentMatchCallKillEpochAttrHelper;
using ConcurrentReportKillEpoch =
    ObeliskSimulationDialect::ConcurrentReportKillEpochAttrHelper;
using ConsequentEndOfSimulationStrength =
    ObeliskSimulationDialect::ConsequentEndOfSimulationStrengthAttrHelper;
using ConsequentFirstMatchEquivalence =
    ObeliskSimulationDialect::ConsequentFirstMatchEquivalenceAttrHelper;
using CoverSequencePerMatch =
    ObeliskSimulationDialect::CoverSequencePerMatchAttrHelper;
using AssertionCoverageReport =
    ObeliskSimulationDialect::AssertionCoverageReportAttrHelper;
using DefaultAssertionFailure =
    ObeliskSimulationDialect::DefaultAssertionFailureAttrHelper;
using DeferredEvaluator = ObeliskSimulationDialect::DeferredEvaluatorAttrHelper;
using DeferredResult = ObeliskSimulationDialect::DeferredResultAttrHelper;
using EmptyAntecedentNonoverlap =
    ObeliskSimulationDialect::EmptyAntecedentNonoverlapAttrHelper;
using EndOfSimulationStrength =
    ObeliskSimulationDialect::EndOfSimulationStrengthAttrHelper;
using ExpectBoundedAlternatives =
    ObeliskSimulationDialect::ExpectBoundedAlternativesAttrHelper;
using ExpectBoundedBranching =
    ObeliskSimulationDialect::ExpectBoundedBranchingAttrHelper;
using ExpectBoundedHorizon =
    ObeliskSimulationDialect::ExpectBoundedHorizonAttrHelper;
using ExpectBoundedStateWords =
    ObeliskSimulationDialect::ExpectBoundedStateWordsAttrHelper;
using ExpectDonePath = ObeliskSimulationDialect::ExpectDonePathAttrHelper;
using ExpectEosCoordinator =
    ObeliskSimulationDialect::ExpectEosCoordinatorAttrHelper;
using ExpectMonitor = ObeliskSimulationDialect::ExpectMonitorAttrHelper;
using ExpectMonitorActor =
    ObeliskSimulationDialect::ExpectMonitorActorAttrHelper;
using ExpectOneCycleAlternatives =
    ObeliskSimulationDialect::ExpectOneCycleAlternativesAttrHelper;
using ExpectOneCycleBranching =
    ObeliskSimulationDialect::ExpectOneCycleBranchingAttrHelper;
using ExpectOperandStrength =
    ObeliskSimulationDialect::ExpectOperandStrengthAttrHelper;
using ExpectResultPath = ObeliskSimulationDialect::ExpectResultPathAttrHelper;
using FirstMatchMonitor = ObeliskSimulationDialect::FirstMatchMonitorAttrHelper;
using FirstMatchPriority =
    ObeliskSimulationDialect::FirstMatchPriorityAttrHelper;
using FirstMatchPriorityGroups =
    ObeliskSimulationDialect::FirstMatchPriorityGroupsAttrHelper;
using FollowedByMonitor = ObeliskSimulationDialect::FollowedByMonitorAttrHelper;
using GlobalFutureMonitor =
    ObeliskSimulationDialect::GlobalFutureMonitorAttrHelper;
using GlobalFutureResolver =
    ObeliskSimulationDialect::GlobalFutureResolverAttrHelper;
using GlobalFutureWait = ObeliskSimulationDialect::GlobalFutureWaitAttrHelper;
using ImplicationAntecedent =
    ObeliskSimulationDialect::ImplicationAntecedentAttrHelper;
using ImplicationAntecedentFailure =
    ObeliskSimulationDialect::ImplicationAntecedentFailureAttrHelper;
using MixedEmptyAntecedentCurrentTickChannels =
    ObeliskSimulationDialect::MixedEmptyAntecedentCurrentTickChannelsAttrHelper;
using MixedEmptyAntecedentHandoffChannels =
    ObeliskSimulationDialect::MixedEmptyAntecedentHandoffChannelsAttrHelper;
using MixedEmptyAntecedentNonoverlap =
    ObeliskSimulationDialect::MixedEmptyAntecedentNonoverlapAttrHelper;
using MonitorCallback = ObeliskSimulationDialect::MonitorCallbackAttrHelper;
using MonitorObservationComplete =
    ObeliskSimulationDialect::MonitorObservationCompleteAttrHelper;
using MulticlockSequenceCoordinator =
    ObeliskSimulationDialect::MulticlockSequenceCoordinatorAttrHelper;
using MulticlockSequenceEosCoordinator =
    ObeliskSimulationDialect::MulticlockSequenceEosCoordinatorAttrHelper;
using NegatedOperandEndOfSimulationStrength =
    ObeliskSimulationDialect::NegatedOperandEndOfSimulationStrengthAttrHelper;
using OverlappedEmptyMatchesIgnored =
    ObeliskSimulationDialect::OverlappedEmptyMatchesIgnoredAttrHelper;
using PersistentAntecedentFinalHandoff =
    ObeliskSimulationDialect::PersistentAntecedentFinalHandoffAttrHelper;
using PersistentAntecedentHandoff =
    ObeliskSimulationDialect::PersistentAntecedentHandoffAttrHelper;
using PersistentAntecedentHandoffStates =
    ObeliskSimulationDialect::PersistentAntecedentHandoffStatesAttrHelper;
using PersistentDelayAggregateTokens =
    ObeliskSimulationDialect::PersistentDelayAggregateTokensAttrHelper;
using PersistentDelayAllMatches =
    ObeliskSimulationDialect::PersistentDelayAllMatchesAttrHelper;
using PersistentDelayImplication =
    ObeliskSimulationDialect::PersistentDelayImplicationAttrHelper;
using PersistentDelayMinimum =
    ObeliskSimulationDialect::PersistentDelayMinimumAttrHelper;
using PersistentDelayMonitor =
    ObeliskSimulationDialect::PersistentDelayMonitorAttrHelper;
using PersistentDelayNonoverlapped =
    ObeliskSimulationDialect::PersistentDelayNonoverlappedAttrHelper;
using PersistentDelayPrefixHorizon =
    ObeliskSimulationDialect::PersistentDelayPrefixHorizonAttrHelper;
using PersistentFirstMatchEquivalence =
    ObeliskSimulationDialect::PersistentFirstMatchEquivalenceAttrHelper;
using PersistentImplicationActivation =
    ObeliskSimulationDialect::PersistentImplicationActivationAttrHelper;
using PersistentImplicationAntecedentResult =
    ObeliskSimulationDialect::PersistentImplicationAntecedentResultAttrHelper;
using PersistentImplicationHandoff =
    ObeliskSimulationDialect::PersistentImplicationHandoffAttrHelper;
using PersistentMonitor = ObeliskSimulationDialect::PersistentMonitorAttrHelper;
using PersistentRepetitionAllMatches =
    ObeliskSimulationDialect::PersistentRepetitionAllMatchesAttrHelper;
using PersistentRepetitionAntecedent =
    ObeliskSimulationDialect::PersistentRepetitionAntecedentAttrHelper;
using PersistentRepetitionDfa =
    ObeliskSimulationDialect::PersistentRepetitionDfaAttrHelper;
using PersistentRepetitionImplication =
    ObeliskSimulationDialect::PersistentRepetitionImplicationAttrHelper;
using PersistentRepetitionKind =
    ObeliskSimulationDialect::PersistentRepetitionKindAttrHelper;
using PersistentRepetitionMax =
    ObeliskSimulationDialect::PersistentRepetitionMaxAttrHelper;
using PersistentRepetitionMin =
    ObeliskSimulationDialect::PersistentRepetitionMinAttrHelper;
using PersistentRepetitionMonitor =
    ObeliskSimulationDialect::PersistentRepetitionMonitorAttrHelper;
using PersistentRepetitionNonoverlapped =
    ObeliskSimulationDialect::PersistentRepetitionNonoverlappedAttrHelper;
using PersistentRepetitionStates =
    ObeliskSimulationDialect::PersistentRepetitionStatesAttrHelper;
using PersistentRepetitionUnbounded =
    ObeliskSimulationDialect::PersistentRepetitionUnboundedAttrHelper;
using PersistentUnaryAggregateTokens =
    ObeliskSimulationDialect::PersistentUnaryAggregateTokensAttrHelper;
using PersistentUnaryImplication =
    ObeliskSimulationDialect::PersistentUnaryImplicationAttrHelper;
using PersistentUnaryKind =
    ObeliskSimulationDialect::PersistentUnaryKindAttrHelper;
using PersistentUnaryMinimum =
    ObeliskSimulationDialect::PersistentUnaryMinimumAttrHelper;
using PersistentUnaryMonitor =
    ObeliskSimulationDialect::PersistentUnaryMonitorAttrHelper;
using PersistentUnaryNonoverlapped =
    ObeliskSimulationDialect::PersistentUnaryNonoverlappedAttrHelper;
using PersistentUntilAggregateTokens =
    ObeliskSimulationDialect::PersistentUntilAggregateTokensAttrHelper;
using PersistentUntilImplication =
    ObeliskSimulationDialect::PersistentUntilImplicationAttrHelper;
using PersistentUntilInclusive =
    ObeliskSimulationDialect::PersistentUntilInclusiveAttrHelper;
using PersistentUntilKind =
    ObeliskSimulationDialect::PersistentUntilKindAttrHelper;
using PersistentUntilMonitor =
    ObeliskSimulationDialect::PersistentUntilMonitorAttrHelper;
using PersistentUntilNonoverlapped =
    ObeliskSimulationDialect::PersistentUntilNonoverlappedAttrHelper;
using PersistentUntilStrong =
    ObeliskSimulationDialect::PersistentUntilStrongAttrHelper;
using ProceduralAssertionAttempt =
    ObeliskSimulationDialect::ProceduralAssertionAttemptAttrHelper;
using ProceduralAssertionEpochReference =
    ObeliskSimulationDialect::ProceduralAssertionEpochReferenceAttrHelper;
using ProceduralAssertionEpochValue =
    ObeliskSimulationDialect::ProceduralAssertionEpochValueAttrHelper;
using PropertyAbortAction =
    ObeliskSimulationDialect::PropertyAbortActionAttrHelper;
using SampleDefaultConstant =
    ObeliskSimulationDialect::SampleDefaultConstantAttrHelper;
using SampleDefaultCurrent =
    ObeliskSimulationDialect::SampleDefaultCurrentAttrHelper;
using SampleDefaultDynamic =
    ObeliskSimulationDialect::SampleDefaultDynamicAttrHelper;
using SampleDefaultSnapshot =
    ObeliskSimulationDialect::SampleDefaultSnapshotAttrHelper;
using SequenceEndpointDefaultClock =
    ObeliskSimulationDialect::SequenceEndpointDefaultClockAttrHelper;
using SequenceEndpointEvent =
    ObeliskSimulationDialect::SequenceEndpointEventAttrHelper;
using SequenceEndpointMonitor =
    ObeliskSimulationDialect::SequenceEndpointMonitorAttrHelper;
using SequenceEndpointPath =
    ObeliskSimulationDialect::SequenceEndpointPathAttrHelper;
using StrongWeakMonitor = ObeliskSimulationDialect::StrongWeakMonitorAttrHelper;
using SvaTransitionNormalForm =
    ObeliskSimulationDialect::SvaTransitionNormalFormAttrHelper;
using SynchronousPropertyAbort =
    ObeliskSimulationDialect::SynchronousPropertyAbortAttrHelper;
using TemporalPropertyNegation =
    ObeliskSimulationDialect::TemporalPropertyNegationAttrHelper;
using TemporalPropertyNegationOutsideAbort =
    ObeliskSimulationDialect::TemporalPropertyNegationOutsideAbortAttrHelper;
using VacuousSequenceAlternatives =
    ObeliskSimulationDialect::VacuousSequenceAlternativesAttrHelper;
using ContinuousStore = ObeliskSimulationDialect::ContinuousStoreAttrHelper;
using DelayQuantum = ObeliskSimulationDialect::DelayQuantumAttrHelper;
using DelayScale = ObeliskSimulationDialect::DelayScaleAttrHelper;
using DelayedNet = ObeliskSimulationDialect::DelayedNetAttrHelper;
using InitialDriverX = ObeliskSimulationDialect::InitialDriverXAttrHelper;
using InterconnectLeafPath =
    ObeliskSimulationDialect::InterconnectLeafPathAttrHelper;
using InterconnectLeaves =
    ObeliskSimulationDialect::InterconnectLeavesAttrHelper;
using InterconnectNettype =
    ObeliskSimulationDialect::InterconnectNettypeAttrHelper;
using InterconnectVpiType =
    ObeliskSimulationDialect::InterconnectVpiTypeAttrHelper;
using MosTopologyIds = ObeliskSimulationDialect::MosTopologyIdsAttrHelper;
using NegativeTimingAdjusted =
    ObeliskSimulationDialect::NegativeTimingAdjustedAttrHelper;
using NegativeTimingDelayCommit =
    ObeliskSimulationDialect::NegativeTimingDelayCommitAttrHelper;
using NegativeTimingDelayMonitor =
    ObeliskSimulationDialect::NegativeTimingDelayMonitorAttrHelper;
using NegativeTimingTransportActivation =
    ObeliskSimulationDialect::NegativeTimingTransportActivationAttrHelper;
using PassSwitchIds = ObeliskSimulationDialect::PassSwitchIdsAttrHelper;
using PropagationDelays = ObeliskSimulationDialect::PropagationDelaysAttrHelper;
using ResolutionFunctionPath =
    ObeliskSimulationDialect::ResolutionFunctionPathAttrHelper;
using ResolutionFunctionSymbol =
    ObeliskSimulationDialect::ResolutionFunctionSymbolAttrHelper;
using SkewDeadlineHelper =
    ObeliskSimulationDialect::SkewDeadlineHelperAttrHelper;
using StrengthBank = ObeliskSimulationDialect::StrengthBankAttrHelper;
using StrengthDriverBank =
    ObeliskSimulationDialect::StrengthDriverBankAttrHelper;
using StrengthGroup = ObeliskSimulationDialect::StrengthGroupAttrHelper;
using TimingAdjustedTicks =
    ObeliskSimulationDialect::TimingAdjustedTicksAttrHelper;
using TimingCheckArgTicks =
    ObeliskSimulationDialect::TimingCheckArgTicksAttrHelper;
using TimingCheckCoordinator =
    ObeliskSimulationDialect::TimingCheckCoordinatorAttrHelper;
using TimingDelayedSourceDelays =
    ObeliskSimulationDialect::TimingDelayedSourceDelaysAttrHelper;
using TimingDelayedStorageIds =
    ObeliskSimulationDialect::TimingDelayedStorageIdsAttrHelper;
using TimingDelayedTerminalIndices =
    ObeliskSimulationDialect::TimingDelayedTerminalIndicesAttrHelper;
using TimingPathMonitorRules =
    ObeliskSimulationDialect::TimingPathMonitorRulesAttrHelper;
using TimingPathRules = ObeliskSimulationDialect::TimingPathRulesAttrHelper;
using TimingTimerHelper = ObeliskSimulationDialect::TimingTimerHelperAttrHelper;
using TimingTimerStorage =
    ObeliskSimulationDialect::TimingTimerStorageAttrHelper;
using UdpMetadata = ObeliskSimulationDialect::UdpMetadataAttrHelper;
using UserDefinedNet = ObeliskSimulationDialect::UserDefinedNetAttrHelper;
using UserNetDriver = ObeliskSimulationDialect::UserNetDriverAttrHelper;
using UserNetRawDrive = ObeliskSimulationDialect::UserNetRawDriveAttrHelper;
using AggregateMemberInitializers =
    ObeliskSimulationDialect::AggregateMemberInitializersAttrHelper;
using AutomaticReferenceCapture =
    ObeliskSimulationDialect::AutomaticReferenceCaptureAttrHelper;
using Bindings = ObeliskSimulationDialect::BindingsAttrHelper;
using Callee = ObeliskSimulationDialect::CalleeAttrHelper;
using CalleeCaptures = ObeliskSimulationDialect::CalleeCapturesAttrHelper;
using CalleeFormals = ObeliskSimulationDialect::CalleeFormalsAttrHelper;
using CalleeReadCaptures =
    ObeliskSimulationDialect::CalleeReadCapturesAttrHelper;
using CalleeWrittenCaptures =
    ObeliskSimulationDialect::CalleeWrittenCapturesAttrHelper;
using CaptureKind = ObeliskSimulationDialect::CaptureKindAttrHelper;
using ClassBitstreamAllowHiddenRoot =
    ObeliskSimulationDialect::ClassBitstreamAllowHiddenRootAttrHelper;
using ClassBitstreamBytecodeFunction =
    ObeliskSimulationDialect::ClassBitstreamBytecodeFunctionAttrHelper;
using ClassBitstreamBytecodeSite =
    ObeliskSimulationDialect::ClassBitstreamBytecodeSiteAttrHelper;
using ClassBitstreamMember =
    ObeliskSimulationDialect::ClassBitstreamMemberAttrHelper;
using ClassBitstreamSource =
    ObeliskSimulationDialect::ClassBitstreamSourceAttrHelper;
using ClassBitstreamVisibility =
    ObeliskSimulationDialect::ClassBitstreamVisibilityAttrHelper;
using ClassBitstreamWidth =
    ObeliskSimulationDialect::ClassBitstreamWidthAttrHelper;
using ClassField = ObeliskSimulationDialect::ClassFieldAttrHelper;
using ClassInstance = ObeliskSimulationDialect::ClassInstanceAttrHelper;
using ClassMethod = ObeliskSimulationDialect::ClassMethodAttrHelper;
using ClassSignature = ObeliskSimulationDialect::ClassSignatureAttrHelper;
using ClassSlot = ObeliskSimulationDialect::ClassSlotAttrHelper;
using ClassSuper = ObeliskSimulationDialect::ClassSuperAttrHelper;
using ClassVirtual = ObeliskSimulationDialect::ClassVirtualAttrHelper;
using ConstantValue = ObeliskSimulationDialect::ConstantValueAttrHelper;
using Constructor = ObeliskSimulationDialect::ConstructorAttrHelper;
using ControlTargetId = ObeliskSimulationDialect::ControlTargetIdAttrHelper;
using CovergroupBlockEventSampleEvaluator =
    ObeliskSimulationDialect::CovergroupBlockEventSampleEvaluatorAttrHelper;
using CovergroupEventSampleEvaluator =
    ObeliskSimulationDialect::CovergroupEventSampleEvaluatorAttrHelper;
using CovergroupStrobeSampleEvaluator =
    ObeliskSimulationDialect::CovergroupStrobeSampleEvaluatorAttrHelper;
using EventExplicitInitializer =
    ObeliskSimulationDialect::EventExplicitInitializerAttrHelper;
using ForeachIteratorObserverCapture =
    ObeliskSimulationDialect::ForeachIteratorObserverCaptureAttrHelper;
using ForkCodeUnitId = ObeliskSimulationDialect::ForkCodeUnitIdAttrHelper;
using InitializeField = ObeliskSimulationDialect::InitializeFieldAttrHelper;
using InitializeNet = ObeliskSimulationDialect::InitializeNetAttrHelper;
using InitializeSampleDefault =
    ObeliskSimulationDialect::InitializeSampleDefaultAttrHelper;
using InitializeStatic = ObeliskSimulationDialect::InitializeStaticAttrHelper;
using InitializeSubelement =
    ObeliskSimulationDialect::InitializeSubelementAttrHelper;
using InitializeTypeOptionKind =
    ObeliskSimulationDialect::InitializeTypeOptionKindAttrHelper;
using InitializeTypeOptionOwner =
    ObeliskSimulationDialect::InitializeTypeOptionOwnerAttrHelper;
using IsTask = ObeliskSimulationDialect::IsTaskAttrHelper;
using Lowered = ObeliskSimulationDialect::LoweredAttrHelper;
using NbaEventCodeUnitId =
    ObeliskSimulationDialect::NbaEventCodeUnitIdAttrHelper;
using NbaEventHierarchy = ObeliskSimulationDialect::NbaEventHierarchyAttrHelper;
using Observer = ObeliskSimulationDialect::ObserverAttrHelper;
using ObserverCaptures = ObeliskSimulationDialect::ObserverCapturesAttrHelper;
using ObserverCoercedType =
    ObeliskSimulationDialect::ObserverCoercedTypeAttrHelper;
using ObserverDependencies =
    ObeliskSimulationDialect::ObserverDependenciesAttrHelper;
using ObserverResult = ObeliskSimulationDialect::ObserverResultAttrHelper;
using Placeholder = ObeliskSimulationDialect::PlaceholderAttrHelper;
using PreparedInitializer =
    ObeliskSimulationDialect::PreparedInitializerAttrHelper;
using ResumableControlTarget =
    ObeliskSimulationDialect::ResumableControlTargetAttrHelper;
using ExternalControlTarget =
    ObeliskSimulationDialect::ExternalControlTargetAttrHelper;
using ReturnVariablePath =
    ObeliskSimulationDialect::ReturnVariablePathAttrHelper;
using SampledObserver = ObeliskSimulationDialect::SampledObserverAttrHelper;
using StaticClassProperty =
    ObeliskSimulationDialect::StaticClassPropertyAttrHelper;
using StaticClassReceiver =
    ObeliskSimulationDialect::StaticClassReceiverAttrHelper;
using StaticInitializer = ObeliskSimulationDialect::StaticInitializerAttrHelper;
using StaticNetConstant = ObeliskSimulationDialect::StaticNetConstantAttrHelper;
using StaticSiteId = ObeliskSimulationDialect::StaticSiteIdAttrHelper;
using SubroutineStorage = ObeliskSimulationDialect::SubroutineStorageAttrHelper;
using ThisArgument = ObeliskSimulationDialect::ThisArgumentAttrHelper;
using TypeOptionItemQualifier =
    ObeliskSimulationDialect::TypeOptionItemQualifierAttrHelper;
using VirtualInterfaceCallees =
    ObeliskSimulationDialect::VirtualInterfaceCalleesAttrHelper;
using VirtualInterfaceClockEvents =
    ObeliskSimulationDialect::VirtualInterfaceClockEventsAttrHelper;
using VirtualInterfaceClockingOutput =
    ObeliskSimulationDialect::VirtualInterfaceClockingOutputAttrHelper;
using VirtualInterfaceMember =
    ObeliskSimulationDialect::VirtualInterfaceMemberAttrHelper;
using VirtualInterfaceParentMember =
    ObeliskSimulationDialect::VirtualInterfaceParentMemberAttrHelper;
using VirtualInterfaceReceiverMembers =
    ObeliskSimulationDialect::VirtualInterfaceReceiverMembersAttrHelper;
using VoidFunction = ObeliskSimulationDialect::VoidFunctionAttrHelper;
using ActivationDefaultSlot =
    ObeliskSimulationDialect::ActivationDefaultSlotAttrHelper;
using BytecodeReadOnly = ObeliskSimulationDialect::BytecodeReadOnlyAttrHelper;
using DescriptorAggregateType =
    ObeliskSimulationDialect::DescriptorAggregateTypeAttrHelper;
using DescriptorId = ObeliskSimulationDialect::DescriptorIdAttrHelper;
using DescriptorIndices = ObeliskSimulationDialect::DescriptorIndicesAttrHelper;
using DescriptorLow = ObeliskSimulationDialect::DescriptorLowAttrHelper;
using DescriptorPackedLow =
    ObeliskSimulationDialect::DescriptorPackedLowAttrHelper;
using DescriptorRootType =
    ObeliskSimulationDialect::DescriptorRootTypeAttrHelper;
using FlushedPrivateSlot =
    ObeliskSimulationDialect::FlushedPrivateSlotAttrHelper;
using GcPollFree = ObeliskSimulationDialect::GcPollFreeAttrHelper;
using HierarchicalName = ObeliskSimulationDialect::HierarchicalNameAttrHelper;
using Rematerialized = ObeliskSimulationDialect::RematerializedAttrHelper;
using VpiAnchor = ObeliskSimulationDialect::VpiAnchorAttrHelper;
using VpiStatementId = ObeliskSimulationDialect::VpiStatementIdAttrHelper;
using VpiCallSite = ObeliskSimulationDialect::VpiCallSiteAttrHelper;
using VpiFrameInstrumentation =
    ObeliskSimulationDialect::VpiFrameInstrumentationAttrHelper;
using VpiAutomatic = ObeliskSimulationDialect::VpiAutomaticAttrHelper;
using VpiCellInstance = ObeliskSimulationDialect::VpiCellInstanceAttrHelper;
using VpiDefinitionName = ObeliskSimulationDialect::VpiDefinitionNameAttrHelper;
using VpiIdentityDelegated =
    ObeliskSimulationDialect::VpiIdentityDelegatedAttrHelper;
using VpiBoxType = ObeliskSimulationDialect::VpiBoxTypeAttrHelper;
using VpiSourceTypeIdentity =
    ObeliskSimulationDialect::VpiSourceTypeIdentityAttrHelper;
using VpiTop = ObeliskSimulationDialect::VpiTopAttrHelper;
using DumpportsScope = ObeliskSimulationDialect::DumpportsScopeAttrHelper;
using DynamicCastEnumValues =
    ObeliskSimulationDialect::DynamicCastEnumValuesAttrHelper;
using DynamicCastTask = ObeliskSimulationDialect::DynamicCastTaskAttrHelper;
using EnumFormatNames = ObeliskSimulationDialect::EnumFormatNamesAttrHelper;
using EnumFormatValues = ObeliskSimulationDialect::EnumFormatValuesAttrHelper;
using EnumFormats = ObeliskSimulationDialect::EnumFormatsAttrHelper;
using EnumMethodNames = ObeliskSimulationDialect::EnumMethodNamesAttrHelper;
using EnumMethodValues = ObeliskSimulationDialect::EnumMethodValuesAttrHelper;
using BooleanMinimizations =
    ObeliskSimulationDialect::BooleanMinimizationsAttrHelper;

} // namespace obelisk::sim::metadata

namespace obelisk::sim {

using discardable::get;
using discardable::has;
using discardable::mark;
using discardable::named;
using discardable::remove;
using discardable::set;

} // namespace obelisk::sim

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONDIALECT_H
