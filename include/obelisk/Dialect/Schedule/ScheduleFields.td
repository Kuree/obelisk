// One schema for scheduling metadata keys and their MLIR payload types.
// Field names are generated from this schema; consumers use the typed enum.
class ScheduleField<string name, string wire, string type> {
  string cppName = name;
  string wireName = wire;
  string attributeType = type;
}
def EvalActivationEntry : ScheduleField<"EvalActivationEntry", "schedule.eval.activation_entry",
    "::mlir::IntegerAttr">;
def EvalAllowedCallees : ScheduleField<"EvalAllowedCallees", "schedule.eval.allowed_callees",
    "::mlir::ArrayAttr">;
def EvalBody : ScheduleField<"EvalBody", "schedule.eval.body",
    "::mlir::FlatSymbolRefAttr">;
def EvalBorrowedCaptures : ScheduleField<"EvalBorrowedCaptures", "schedule.eval.borrowed_captures",
    "::mlir::UnitAttr">;
def EvalCacheHintWords : ScheduleField<"EvalCacheHintWords", "schedule.eval.cache_hint_words",
    "::mlir::IntegerAttr">;
def EvalCallClosureRoot : ScheduleField<"EvalCallClosureRoot", "schedule.eval.call_closure_root",
    "::mlir::UnitAttr">;
def EvalCheckpointOnly : ScheduleField<"EvalCheckpointOnly", "schedule.eval.checkpoint_only",
    "::mlir::UnitAttr">;
def EvalCheckpointRoutes : ScheduleField<"EvalCheckpointRoutes", "schedule.eval.checkpoint_routes",
    "::mlir::ArrayAttr">;
def EvalCheckpointSafe : ScheduleField<"EvalCheckpointSafe", "schedule.eval.checkpoint_safe",
    "::mlir::UnitAttr">;
def EvalCoalescedSlots : ScheduleField<"EvalCoalescedSlots", "schedule.eval.coalesced_slots",
    "::mlir::IntegerAttr">;
def EvalCompactNbaMetadata : ScheduleField<"EvalCompactNbaMetadata", "schedule.eval.compact_nba_metadata",
    "::mlir::UnitAttr">;
def EvalConditionallyTwoState : ScheduleField<"EvalConditionallyTwoState", "schedule.eval.conditionally_two_state",
    "::mlir::UnitAttr">;
def EvalContinuation : ScheduleField<"EvalContinuation", "schedule.eval.continuation",
    "::mlir::IntegerAttr">;
def EvalDataflowCandidate : ScheduleField<"EvalDataflowCandidate", "schedule.eval.dataflow_candidate",
    "::mlir::FlatSymbolRefAttr">;
def EvalDataflowExecutor : ScheduleField<"EvalDataflowExecutor", "schedule.eval.dataflow_executor",
    "::mlir::FlatSymbolRefAttr">;
def EvalDataflowFallback : ScheduleField<"EvalDataflowFallback", "schedule.eval.dataflow_fallback",
    "::mlir::FlatSymbolRefAttr">;
def EvalDataflowSlots : ScheduleField<"EvalDataflowSlots", "schedule.eval.dataflow_slots",
    "::mlir::IntegerAttr">;
def EvalDirectCall : ScheduleField<"EvalDirectCall", "schedule.eval.direct_call",
    "::mlir::UnitAttr">;
def EvalDirectFragment : ScheduleField<"EvalDirectFragment", "schedule.eval.direct_fragment",
    "::mlir::IntegerAttr">;
def EvalDirectOutput : ScheduleField<"EvalDirectOutput", "schedule.eval.direct_output",
    "::mlir::UnitAttr">;
def EvalDiscardableStore : ScheduleField<"EvalDiscardableStore", "schedule.eval.discardable_store",
    "::mlir::UnitAttr">;
def EvalForceTwoState : ScheduleField<"EvalForceTwoState", "schedule.eval.force_two_state",
    "::mlir::UnitAttr">;
def EvalFourStateSource : ScheduleField<"EvalFourStateSource", "schedule.eval.four_state_source",
    "::mlir::FlatSymbolRefAttr">;
def EvalFusionGroup : ScheduleField<"EvalFusionGroup", "schedule.eval.fusion_group",
    "::mlir::IntegerAttr">;
def EvalGenerated : ScheduleField<"EvalGenerated", "schedule.eval.generated",
    "::mlir::UnitAttr">;
def EvalGroupChildren : ScheduleField<"EvalGroupChildren", "schedule.eval.group_children",
    "::mlir::ArrayAttr">;
def EvalGroupDomainSelected : ScheduleField<"EvalGroupDomainSelected", "schedule.eval.group_domain_selected",
    "::mlir::UnitAttr">;
def EvalGroupIngress : ScheduleField<"EvalGroupIngress", "schedule.eval.group_ingress",
    "::mlir::FlatSymbolRefAttr">;
def EvalGroupMember : ScheduleField<"EvalGroupMember", "schedule.eval.group_member",
    "::mlir::IntegerAttr">;
def EvalGroupOwner : ScheduleField<"EvalGroupOwner", "schedule.eval.group_owner",
    "::mlir::IntegerAttr">;
def EvalGroupParent : ScheduleField<"EvalGroupParent", "schedule.eval.group_parent",
    "::mlir::FlatSymbolRefAttr">;
def EvalInductiveTwoState : ScheduleField<"EvalInductiveTwoState", "schedule.eval.inductive_two_state",
    "::mlir::UnitAttr">;
def EvalInductiveTwoStateAccess : ScheduleField<"EvalInductiveTwoStateAccess", "schedule.eval.inductive_two_state_access",
    "::mlir::UnitAttr">;
def EvalInfallible : ScheduleField<"EvalInfallible", "schedule.eval.infallible",
    "::mlir::UnitAttr">;
def EvalInheritedTwoStateCheckpoint : ScheduleField<"EvalInheritedTwoStateCheckpoint", "schedule.eval.inherited_two_state_checkpoint",
    "::mlir::UnitAttr">;
def EvalInstanceCoordinator : ScheduleField<"EvalInstanceCoordinator", "schedule.eval.instance_coordinator",
    "::mlir::UnitAttr">;
def EvalKeepFourStateNba : ScheduleField<"EvalKeepFourStateNba", "schedule.eval.keep_four_state_nba",
    "::mlir::UnitAttr">;
def EvalKernelProofDependencies : ScheduleField<"EvalKernelProofDependencies", "schedule.eval.kernel_proof_dependencies",
    "::mlir::ArrayAttr">;
def EvalLocalPromotionRanges : ScheduleField<"EvalLocalPromotionRanges", "schedule.eval.local_promotion_ranges",
    "::mlir::DenseI64ArrayAttr">;
def EvalMaterializedGroupCalls : ScheduleField<"EvalMaterializedGroupCalls", "schedule.eval.materialized_group_calls",
    "::mlir::IntegerAttr">;
def EvalMayTerminate : ScheduleField<"EvalMayTerminate", "schedule.eval.may_terminate",
    "::mlir::UnitAttr">;
def EvalNbaProofDependencies : ScheduleField<"EvalNbaProofDependencies", "schedule.eval.nba_proof_dependencies",
    "::mlir::ArrayAttr">;
def EvalOrderedNbaQueue : ScheduleField<"EvalOrderedNbaQueue", "schedule.eval.ordered_nba_queue",
    "::mlir::UnitAttr">;
def EvalOriginNbaSite : ScheduleField<"EvalOriginNbaSite", "schedule.eval.origin_nba_site",
    "::mlir::IntegerAttr">;
def EvalPathGuardedKnownPreserving : ScheduleField<"EvalPathGuardedKnownPreserving", "schedule.eval.path_guarded_known_preserving",
    "::mlir::UnitAttr">;
def EvalPathGuardedTwoState : ScheduleField<"EvalPathGuardedTwoState", "schedule.eval.path_guarded_two_state",
    "::mlir::UnitAttr">;
def EvalPathKnownPredicate : ScheduleField<"EvalPathKnownPredicate", "schedule.eval.path_known_predicate",
    "::mlir::UnitAttr">;
def EvalPathKnownProbe : ScheduleField<"EvalPathKnownProbe", "schedule.eval.path_known_probe",
    "::mlir::FlatSymbolRefAttr">;
def EvalPathProbeRoutes : ScheduleField<"EvalPathProbeRoutes", "schedule.eval.path_probe_routes",
    "::mlir::ArrayAttr">;
def EvalPredicatedDataflow : ScheduleField<"EvalPredicatedDataflow", "schedule.eval.predicated_dataflow",
    "::mlir::UnitAttr">;
def EvalPreserveNbaUnknown : ScheduleField<"EvalPreserveNbaUnknown", "schedule.eval.preserve_nba_unknown",
    "::mlir::UnitAttr">;
def EvalPrivateHelper : ScheduleField<"EvalPrivateHelper", "schedule.eval.private_helper",
    "::mlir::UnitAttr">;
def EvalProofCoverage : ScheduleField<"EvalProofCoverage", "schedule.eval.proof_coverage",
    "::mlir::DenseI64ArrayAttr">;
def EvalProvenTwoStateCall : ScheduleField<"EvalProvenTwoStateCall", "schedule.eval.proven_two_state_call",
    "::mlir::UnitAttr">;
def EvalRankedMembers : ScheduleField<"EvalRankedMembers", "schedule.eval.ranked_members",
    "::mlir::DenseI32ArrayAttr">;
def EvalRawCaptures : ScheduleField<"EvalRawCaptures", "schedule.eval.raw_captures",
    "::mlir::UnitAttr">;
def EvalReadyWordCount : ScheduleField<"EvalReadyWordCount", "schedule.eval.ready_word_count",
    "::mlir::IntegerAttr">;
def EvalReconstructsContinuationArgs : ScheduleField<"EvalReconstructsContinuationArgs", "schedule.eval.reconstructs_continuation_args",
    "::mlir::UnitAttr">;
def EvalRouteProofDependencies : ScheduleField<"EvalRouteProofDependencies", "schedule.eval.route_proof_dependencies",
    "::obelisk::schedule::RouteProofDependencyAttr">;
def EvalRuntimeCalendar : ScheduleField<"EvalRuntimeCalendar", "schedule.eval.runtime_calendar",
    "::mlir::UnitAttr">;
def EvalRuntimeNbaFallback : ScheduleField<"EvalRuntimeNbaFallback", "schedule.eval.runtime_nba_fallback",
    "::mlir::UnitAttr">;
def EvalRuntimeNbaRequired : ScheduleField<"EvalRuntimeNbaRequired", "schedule.eval.runtime_nba_required",
    "::mlir::UnitAttr">;
def EvalSegmentHelpers : ScheduleField<"EvalSegmentHelpers", "schedule.eval.segment_helpers",
    "::mlir::ArrayAttr">;
def EvalSelectedTwoState : ScheduleField<"EvalSelectedTwoState", "schedule.eval.selected_two_state",
    "::mlir::UnitAttr">;
def EvalSourceOwner : ScheduleField<"EvalSourceOwner", "schedule.eval.source_owner",
    "::obelisk::schedule::SourceOwnerAttr">;
def EvalSourceOwners : ScheduleField<"EvalSourceOwners", "schedule.eval.source_owners",
    "::mlir::ArrayAttr">;
def EvalSsaReadyWords : ScheduleField<"EvalSsaReadyWords", "schedule.eval.ssa_ready_words",
    "::mlir::IntegerAttr">;
def EvalSsaUnknownRanges : ScheduleField<"EvalSsaUnknownRanges", "schedule.eval.ssa_unknown_ranges",
    "::mlir::IntegerAttr">;
def EvalSsaValueRanges : ScheduleField<"EvalSsaValueRanges", "schedule.eval.ssa_value_ranges",
    "::mlir::IntegerAttr">;
def EvalTier2Convergence : ScheduleField<"EvalTier2Convergence", "schedule.eval.tier2_convergence",
    "::mlir::UnitAttr">;
def EvalTrustedTwoStateClosure : ScheduleField<"EvalTrustedTwoStateClosure", "schedule.eval.trusted_two_state_closure",
    "::mlir::UnitAttr">;
def EvalTwoStateVariant : ScheduleField<"EvalTwoStateVariant", "schedule.eval.two_state_variant",
    "::mlir::FlatSymbolRefAttr">;
def EvalTwoStateZeroUnknown : ScheduleField<"EvalTwoStateZeroUnknown", "schedule.eval.two_state_zero_unknown",
    "::mlir::UnitAttr">;
def EvalUnknownWriteRange : ScheduleField<"EvalUnknownWriteRange", "schedule.eval.unknown_write_range",
    "::mlir::DenseI64ArrayAttr">;
def EvalUnsupportedCheckpointOwner : ScheduleField<"EvalUnsupportedCheckpointOwner", "schedule.eval.unsupported_checkpoint_owner",
    "::mlir::StringAttr">;
def EvalUseCanonicalTwoStateNba : ScheduleField<"EvalUseCanonicalTwoStateNba", "schedule.eval.use_canonical_two_state_nba",
    "::mlir::UnitAttr">;
def EvalUseFastTwoStateNba : ScheduleField<"EvalUseFastTwoStateNba", "schedule.eval.use_fast_two_state_nba",
    "::mlir::UnitAttr">;
def NativeAssumeCleanSpecialization : ScheduleField<"NativeAssumeCleanSpecialization", "schedule.native.assume_clean_specialization",
    "::mlir::UnitAttr">;
def NativeCleanEvalBody : ScheduleField<"NativeCleanEvalBody", "schedule.native.clean_eval_body",
    "::mlir::UnitAttr">;
def NativeScheduler : ScheduleField<"NativeScheduler", "schedule.native_scheduler",
    "::obelisk::schedule::NativeSchedulerModeAttr">;
def NativeSchedulerAutoRequested : ScheduleField<"NativeSchedulerAutoRequested", "schedule.native_scheduler.auto_requested",
    "::mlir::UnitAttr">;
def NbaChangeWatched : ScheduleField<"NbaChangeWatched", "schedule.nba.change_watched",
    "::mlir::DenseI64ArrayAttr">;
def NbaTransientObservable : ScheduleField<"NbaTransientObservable", "schedule.nba.transient_observable",
    "::mlir::DenseI64ArrayAttr">;
def BoundedLoopHeader : ScheduleField<"BoundedLoopHeader", "schedule.bounded_loop_header",
    "::mlir::UnitAttr">;
def BoundedLoopLatch : ScheduleField<"BoundedLoopLatch", "schedule.bounded_loop_latch",
    "::mlir::UnitAttr">;
def ClockedControl : ScheduleField<"ClockedControl", "schedule.clocked_control",
    "::mlir::UnitAttr">;
def ClockedSamplePlan : ScheduleField<"ClockedSamplePlan", "schedule.clocked_sample_plan",
    "::obelisk::schedule::ClockedSamplePlanAttr">;
def ComputeKernels : ScheduleField<"ComputeKernels", "schedule.compute_kernels",
    "::mlir::ArrayAttr">;
def PeriodicControl : ScheduleField<"PeriodicControl", "schedule.periodic_control",
    "::mlir::UnitAttr">;
def ProceduralEventWait : ScheduleField<"ProceduralEventWait", "schedule.procedural_event_wait",
    "::mlir::UnitAttr">;
def RepeatingAlwaysWait : ScheduleField<"RepeatingAlwaysWait", "schedule.repeating_always_wait",
    "::mlir::UnitAttr">;
def StaticBodyFusion : ScheduleField<"StaticBodyFusion", "schedule.static_body_fusion",
    "::mlir::ArrayAttr">;
def StaticFusion : ScheduleField<"StaticFusion", "schedule.static_fusion",
    "::mlir::ArrayAttr">;
def StaticSpecialization : ScheduleField<"StaticSpecialization", "schedule.static_specialization",
    "::obelisk::schedule::StaticSpecializationAttr">;
def StaticSuperstep : ScheduleField<"StaticSuperstep", "schedule.static_superstep",
    "::obelisk::schedule::StaticSuperstepAttr">;
def ThreeTierSchedule : ScheduleField<"ThreeTierSchedule", "schedule.three_tier_schedule",
    "::obelisk::schedule::ThreeTierScheduleAttr">;
def TopLevelWildcardWait : ScheduleField<"TopLevelWildcardWait", "schedule.top_level_wildcard_wait",
    "::mlir::UnitAttr">;
def NativeRegionBody : ScheduleField<"NativeRegionBody", "schedule.native.region_body", "::mlir::UnitAttr">;
def NativeGuardedSpecializationBody : ScheduleField<"NativeGuardedSpecializationBody", "schedule.native.guarded_specialization_body", "::mlir::UnitAttr">;
def ObserverCaptureBridge : ScheduleField<"ObserverCaptureBridge", "schedule.observer_capture_bridge", "::mlir::UnitAttr">;
def ConcurrentEosCoordinator : ScheduleField<"ConcurrentEosCoordinator", "schedule.concurrent_eos_coordinator", "::mlir::UnitAttr">;
def PrimitiveName : ScheduleField<"PrimitiveName", "schedule.primitive_name", "::mlir::StringAttr">;
def CovergroupClockingSampler : ScheduleField<"CovergroupClockingSampler", "schedule.covergroup_clocking_sampler", "::mlir::UnitAttr">;
def DeferNetResolution : ScheduleField<"DeferNetResolution", "schedule.defer_net_resolution", "::mlir::UnitAttr">;
def OutlinedPrimitiveMember : ScheduleField<"OutlinedPrimitiveMember", "schedule.outlined_primitive_member", "::mlir::UnitAttr">;
def OutlinedPrimitiveFingerprint : ScheduleField<"OutlinedPrimitiveFingerprint", "schedule.outlined_primitive_fingerprint", "::mlir::StringAttr">;
def ExactDriverId : ScheduleField<"ExactDriverId", "schedule.exact_driver_id", "::mlir::IntegerAttr">;
def ExactDriverLow : ScheduleField<"ExactDriverLow", "schedule.exact_driver_low", "::mlir::IntegerAttr">;
def ComputedEventStartup : ScheduleField<"ComputedEventStartup", "schedule.computed_event_startup", "::mlir::UnitAttr">;
def MaxInlineOps : ScheduleField<"MaxInlineOps", "schedule.native.max_inline_ops", "::mlir::IntegerAttr">;
def EvalTwoStateWrapper : ScheduleField<"EvalTwoStateWrapper", "schedule.eval.two_state_wrapper", "::mlir::UnitAttr">;
def StartsWithoutWaiting : ScheduleField<"StartsWithoutWaiting", "schedule.starts_without_waiting", "::mlir::UnitAttr">;
def PrimeOnSpawn : ScheduleField<"PrimeOnSpawn", "schedule.prime_on_spawn", "::mlir::UnitAttr">;
def DetachedControls : ScheduleField<"DetachedControls", "schedule.detached_controls", "::mlir::UnitAttr">;
def PrioritySignalResume : ScheduleField<"PrioritySignalResume", "schedule.priority_signal_resume", "::mlir::UnitAttr">;
def ProgramOwnerId : ScheduleField<"ProgramOwnerId", "schedule.program_owner_id", "::mlir::IntegerAttr">;
def ConcurrentCancel : ScheduleField<"ConcurrentCancel", "schedule.concurrent_cancel", "::mlir::UnitAttr">;
def ConcurrentAbort : ScheduleField<"ConcurrentAbort", "schedule.concurrent_abort", "::mlir::UnitAttr">;
def ConcurrentCancelLevelTrue : ScheduleField<"ConcurrentCancelLevelTrue", "schedule.concurrent_cancel_level_true", "::mlir::UnitAttr">;
def ConcurrentAbortLevelTrue : ScheduleField<"ConcurrentAbortLevelTrue", "schedule.concurrent_abort_level_true", "::mlir::UnitAttr">;
def EventPrimary : ScheduleField<"EventPrimary", "schedule.event_primary", "::mlir::UnitAttr">;
def ObserverWidth : ScheduleField<"ObserverWidth", "schedule.observer_width", "::mlir::IntegerAttr">;
def ObserverFourState : ScheduleField<"ObserverFourState", "schedule.observer_four_state", "::mlir::BoolAttr">;
def OverrideEvaluator : ScheduleField<"OverrideEvaluator", "schedule.override_evaluator", "::mlir::UnitAttr">;
def ConcurrentEosCounted : ScheduleField<"ConcurrentEosCounted", "schedule.concurrent_eos_counted", "::mlir::UnitAttr">;
def ConcurrentReport : ScheduleField<"ConcurrentReport", "schedule.concurrent_report", "::mlir::UnitAttr">;
def ConcurrentCancelObserver : ScheduleField<"ConcurrentCancelObserver", "schedule.concurrent_cancel_observer", "::mlir::UnitAttr">;
def ConcurrentAbortObserver : ScheduleField<"ConcurrentAbortObserver", "schedule.concurrent_abort_observer", "::mlir::UnitAttr">;
def ConcurrentCancelObserverRequest : ScheduleField<"ConcurrentCancelObserverRequest", "schedule.concurrent_cancel_observer_request", "::mlir::ArrayAttr">;
def ConcurrentAbortObserverRequest : ScheduleField<"ConcurrentAbortObserverRequest", "schedule.concurrent_abort_observer_request", "::mlir::ArrayAttr">;

// Physical scheduling metadata retained across native representation conversion.
def NativeConditionOperandBegin : ScheduleField<"NativeConditionOperandBegin", "schedule.native.condition_operand_begin", "::mlir::IntegerAttr">;
def NativeContinuation : ScheduleField<"NativeContinuation", "schedule.native.continuation", "::mlir::IntegerAttr">;
def NativeContinuationOperandBegin : ScheduleField<"NativeContinuationOperandBegin", "schedule.native.continuation_operand_begin", "::mlir::IntegerAttr">;
def NativeDependencyCaptureIndices : ScheduleField<"NativeDependencyCaptureIndices", "schedule.native.dependency_capture_indices", "::mlir::DenseI32ArrayAttr">;
def NativeDependencyKinds : ScheduleField<"NativeDependencyKinds", "schedule.native.dependency_kinds", "::mlir::DenseI32ArrayAttr">;
def NativeDependencyWidths : ScheduleField<"NativeDependencyWidths", "schedule.native.dependency_widths", "::mlir::DenseI32ArrayAttr">;
def NativeInitialPlaneCounts : ScheduleField<"NativeInitialPlaneCounts", "schedule.native.initial_plane_counts", "::mlir::DenseI32ArrayAttr">;
def NativeObserverFourState : ScheduleField<"NativeObserverFourState", "schedule.native.observer_four_state", "::mlir::BoolAttr">;
def NativeObserverId : ScheduleField<"NativeObserverId", "schedule.native.observer_id", "::mlir::IntegerAttr">;
def NativeObserverWidth : ScheduleField<"NativeObserverWidth", "schedule.native.observer_width", "::mlir::IntegerAttr">;
def NativeWaitOffset : ScheduleField<"NativeWaitOffset", "schedule.native.wait_offset", "::mlir::IntegerAttr">;
def NativeWaitSize : ScheduleField<"NativeWaitSize", "schedule.native.wait_size", "::mlir::IntegerAttr">;
def NativeWaitWidths : ScheduleField<"NativeWaitWidths", "schedule.native.wait_widths", "::mlir::DenseI32ArrayAttr">;
