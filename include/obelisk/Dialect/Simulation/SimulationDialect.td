//===- SimulationDialect.td - Executable Obelisk simulation IR -*- td -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONDIALECT_TD
#define OBELISK_DIALECT_SIMULATION_SIMULATIONDIALECT_TD

include "mlir/IR/DialectBase.td"

def ObeliskSimulationDialect : Dialect {
  let name = "simulation";
  let cppNamespace = "::obelisk::sim";
  let dependentDialects = ["::obelisk::schedule::ScheduleDialect", "::mlir::arith::ArithDialect",
                           "::mlir::math::MathDialect",
                           "::obelisk::runtime::ObeliskRuntimeDialect",
                           "::obelisk::cov::CoverageDialect",
                           "::obelisk::dpi::DPIDialect",
                           "::obelisk::native::NativeDialect"];
  let summary = "Target-independent executable SystemVerilog simulation IR";
  let description = [{
    `simulation` is the SSA and explicit-effect boundary after elaborated
    SystemVerilog semantic IR.  Hierarchy and mutable design objects are
    identified by deterministic numeric descriptors; code is isolated into
    function-like SSACFG regions whose only symbolic references are direct
    sibling calls and process spawns.

    Packed builtin integers are exact two-state values and `logic` values are
    exact four-state values. Operations preserve the four-state plane unless
    their contract explicitly performs truth evaluation, conversion to a
    two-state result, or dynamic-selection fallback. Dynamic selections accept
    integer and four-state indices: an unknown index reads as X in the logic
    domain and zero in the bits domain, and suppresses writes and drives.
    Partially out-of-range selections preserve or update valid positions while
    reads fill invalid positions with X or zero according to the result domain
    and writes ignore invalid positions.
  }];
  let useDefaultAttributePrinterParser = 1;
  let useDefaultTypePrinterParser = 1;
  let usePropertiesForAttributes = 0;
  let hasConstantMaterializer = 1;
  let hasOperationAttrVerify = 1;
  let hasRegionArgAttrVerify = 1;
  let discardableAttrs = (ins
    // Integer range certificates: per function argument, per call or
    // function result (a `#simulation.integer_range` or unit), and symbolic
    // return summaries (a `#simulation.integer_expression` or unit).
    "::obelisk::sim::IntegerRangeAttr":$integer_range,
    "::mlir::ArrayAttr":$result_ranges,
    "::mlir::ArrayAttr":$result_exprs,
    // Transient no-X/Z certificate for one semantic logic result.
    "::mlir::UnitAttr":$known_logic_result,
    // Module: compiled code roots managed values only at GC polls and at
    // calls that may reach one.
    "::mlir::UnitAttr":$gc_polled,
    // Function: the inliner's cached callee-invariant `InlineLegality`.
    "::mlir::IntegerAttr":$inline_legality,
    // Module: driver pulse controls for path delays (IEEE 1800-2023 30.7).
    "::mlir::IntegerAttr":$pulse_reject_percent,
    "::mlir::IntegerAttr":$pulse_error_percent,
    "::mlir::BoolAttr":$pulse_on_detect,
    "::mlir::BoolAttr":$pulse_show_cancelled,
    // Constrained randomization (IEEE 1800-2023 18): class metadata and the
    // randomize(), rand_mode() and constraint_mode() protocols between
    // preparation and unit lowering, and constraint-tree annotations.
    "::mlir::UnitAttr":$constraint_mode,
    "::mlir::IntegerAttr":$constraint_mode_block,
    "::mlir::FlatSymbolRefAttr":$constraint_mode_field,
    "::mlir::IntegerAttr":$constraint_mode_static_storage,
    "::mlir::DenseI64ArrayAttr":$constraint_mode_static_storages,
    // Class symbols, most derived first.
    "::mlir::ArrayAttr":$object_random_dispatch_classes,
    "::mlir::UnitAttr":$rand_mode,
    "::mlir::IntegerAttr":$rand_mode_ancestor_mask,
    "::mlir::FlatSymbolRefAttr":$rand_mode_array_field,
    "::mlir::IntegerAttr":$rand_mode_array_storage,
    "::mlir::TypeAttr":$rand_mode_array_type,
    "::mlir::IntegerAttr":$rand_mode_family_mask,
    "::mlir::IntegerAttr":$rand_mode_property,
    "::mlir::IntegerAttr":$rand_mode_selection_mask,
    // `#simulation.rand_mode_dispatch` entries.
    "::mlir::ArrayAttr":$rand_mode_static_dispatch,
    "::mlir::IntegerAttr":$rand_mode_static_storage,
    "::mlir::IntegerAttr":$rand_mode_struct_depth,
    // `#simulation.random_element_modes` entries.
    "::mlir::ArrayAttr":$rand_mode_struct_element_fields,
    "::mlir::UnitAttr":$random_arithmetic_node,
    "::mlir::UnitAttr":$random_arithmetic_operand,
    "::mlir::IntegerAttr":$random_arithmetic_plane,
    "::mlir::IntegerAttr":$random_array_element_width,
    "::mlir::FlatSymbolRefAttr":$random_array_mode_field,
    "::mlir::IntegerAttr":$random_constraint_block,
    "::mlir::IntegerAttr":$random_constraint_count,
    // `#simulation.random_container` entries.
    "::mlir::ArrayAttr":$random_container_properties,
    // Hidden i64 fields that carry one randc property's permutation state.
    "::mlir::FlatSymbolRefAttr":$random_cycle_key_field,
    "::mlir::FlatSymbolRefAttr":$random_cycle_position_field,
    "::mlir::UnitAttr":$random_evaluation_error,
    "::mlir::IntegerAttr":$random_evaluation_inputs,
    "::mlir::IntegerAttr":$random_function_case_condition,
    "::mlir::UnitAttr":$random_function_conditional,
    "::mlir::UnitAttr":$random_function_expression,
    "::mlir::ArrayAttr":$random_function_order,
    "::mlir::UnitAttr":$random_function_state,
    "::mlir::IntegerAttr":$random_guard_inputs,
    "::mlir::FlatSymbolRefAttr":$random_increment_field,
    // Root-class field containing the 64-bit disabled-property mask.
    "::mlir::FlatSymbolRefAttr":$random_mode_field,
    // Stable base-to-derived index of an effective rand/randc class property.
    "::mlir::IntegerAttr":$random_mode_index,
    // `#simulation.random_nested_constraints` entries.
    "::mlir::ArrayAttr":$random_nested_constraint_modes,
    // `#simulation.random_nested_hooks` entries.
    "::mlir::ArrayAttr":$random_nested_hooks,
    "::mlir::TypeAttr":$random_nested_state_concrete_type,
    "::mlir::FlatSymbolRefAttr":$random_nested_state_field,
    // `#simulation.random_object_step` entries without mode indices.
    "::mlir::ArrayAttr":$random_nested_state_path,
    "::mlir::TypeAttr":$random_nested_state_storage_type,
    // Marks a non-static rand class-handle field as a recursive object edge.
    "::mlir::UnitAttr":$random_object_edge,
    "::obelisk::sim::RandomizeHookAttr":$random_post_hook,
    "::obelisk::sim::RandomizeHookAttr":$random_pre_hook,
    "::mlir::IntegerAttr":$random_priority_arguments,
    "::mlir::IntegerAttr":$random_priority_inputs,
    // `#simulation.random_property` entries.
    "::mlir::ArrayAttr":$random_properties,
    "::mlir::UnitAttr":$random_real_finite,
    "::mlir::IntegerAttr":$random_receiver_index,
    // `#simulation.random_alias_guard` entries.
    "::mlir::ArrayAttr":$random_recursive_alias_guards,
    "::mlir::FlatSymbolRefAttr":$random_state_field,
    // `#simulation.random_struct_member` entries of a structure property.
    "::mlir::ArrayAttr":$random_struct_members,
    "::mlir::IntegerAttr":$random_total_width,
    "::mlir::IntegerAttr":$random_variable,
    "::mlir::IntegerAttr":$random_variable_bit_offset,
    // Typed RandomVariableKindAttr on a direct packed instance rand property.
    "::obelisk::sim::RandomVariableKindAttr":$random_variable_kind,
    // Source signedness of a direct packed random variable.
    "::mlir::BoolAttr":$random_variable_signed,
    "::mlir::UnitAttr":$randomize,
    "::mlir::UnitAttr":$randomize_checker_only,
    "::mlir::FlatSymbolRefAttr":$randomize_constraint_template,
    "::mlir::UnitAttr":$randomize_dispatch,
    "::mlir::UnitAttr":$randomize_explicit_properties,
    "::mlir::FlatSymbolRefAttr":$randomize_helper,
    "::mlir::ArrayAttr":$randomize_helper_captures,
    "::mlir::ArrayAttr":$randomize_helper_read_captures,
    "::mlir::UnitAttr":$randomize_helper_receiver,
    "::mlir::UnitAttr":$randomize_nested_dispatch,
    "::mlir::FlatSymbolRefAttr":$randomize_nested_dispatch_field,
    // `#simulation.random_object_step` entries from the receiver to the
    // dispatched object, and the field symbols along them.
    "::mlir::ArrayAttr":$randomize_nested_dispatch_path,
    "::mlir::ArrayAttr":$randomize_nested_dispatch_selection_path,
    "::mlir::TypeAttr":$randomize_nested_dispatch_storage_type,
    // `#simulation.random_nested_selection` entries.
    "::mlir::ArrayAttr":$randomize_nested_plans,
    "::mlir::FlatSymbolRefAttr":$randomize_plan_class,
    "::mlir::UnitAttr":$randomize_scope,
    "::mlir::IntegerAttr":$static_constraint_storage,
    "::mlir::IntegerAttr":$static_random_array_mode_storage,
    "::mlir::IntegerAttr":$static_random_mode_storage,

    // Assertions (IEEE 1800-2023 16): concurrent monitor construction and the
    // markers that record it, deferred and procedural assertions, and
    // sampled-value defaults.
    "::mlir::UnitAttr":$assertion_action_controlled,
    "::mlir::IntegerAttr":$assertion_control_action,
    "::mlir::IntegerAttr":$assertion_control_action_argument,
    "::mlir::DenseI64ArrayAttr":$assertion_control_assertion_types,
    "::mlir::IntegerAttr":$assertion_control_assertion_types_argument,
    "::mlir::DenseI64ArrayAttr":$assertion_control_depths,
    "::mlir::DenseI64ArrayAttr":$assertion_control_directive_types,
    "::mlir::IntegerAttr":$assertion_control_directive_types_argument,
    "::mlir::DenseI64ArrayAttr":$assertion_control_ids,
    "::mlir::IntegerAttr":$assertion_control_levels_argument,
    "::mlir::IntegerAttr":$assertion_control_target_id,
    "::mlir::UnitAttr":$assertion_controlled,
    "::mlir::UnitAttr":$assertion_kill_controlled,
    "::mlir::ArrayAttr":$assertion_local_types,
    "::mlir::StringAttr":$assertion_path,
    "::mlir::IntegerAttr":$assertion_target_id,
    "::mlir::UnitAttr":$asynchronous_property_abort,
    // Monitor function: one `#simulation.boolean_minimization` per minimized
    // property operand, in lowering order.
    "::mlir::ArrayAttr":$boolean_minimizations,
    "::mlir::IntegerAttr":$bounded_antecedent_horizon,
    "::mlir::IntegerAttr":$branching_antecedent_alternatives,
    "::mlir::UnitAttr":$branching_antecedent_backedge,
    "::mlir::IntegerAttr":$branching_antecedent_channel,
    "::mlir::StringAttr":$branching_antecedent_consequent_eos_strength,
    "::mlir::UnitAttr":$branching_antecedent_consequent_trigger,
    "::mlir::UnitAttr":$branching_antecedent_eos_coalescer,
    "::mlir::UnitAttr":$branching_antecedent_eos_report,
    "::mlir::StringAttr":$branching_antecedent_eos_result,
    "::mlir::IntegerAttr":$branching_antecedent_eos_source_age,
    "::mlir::UnitAttr":$branching_antecedent_existential_failure,
    "::mlir::UnitAttr":$branching_antecedent_existential_success,
    "::mlir::IntegerAttr":$branching_antecedent_match_channels,
    "::mlir::UnitAttr":$branching_antecedent_matched_history,
    "::mlir::UnitAttr":$branching_antecedent_monitor,
    "::mlir::UnitAttr":$branching_antecedent_result_cancel,
    "::mlir::UnitAttr":$branching_antecedent_result_coalescer,
    "::mlir::IntegerAttr":$branching_antecedent_result_horizon,
    "::mlir::UnitAttr":$branching_antecedent_universal_failure,
    "::mlir::UnitAttr":$branching_antecedent_universal_success,
    "::mlir::UnitAttr":$branching_antecedent_vacuity,
    "::mlir::IntegerAttr":$branching_consequent_alternative,
    "::mlir::UnitAttr":$branching_consequent_alternative_cancel,
    "::mlir::IntegerAttr":$branching_consequent_alternatives,
    "::mlir::UnitAttr":$branching_consequent_backedge,
    "::mlir::UnitAttr":$branching_consequent_eos_coalescer,
    "::mlir::StringAttr":$branching_consequent_intrinsic_eos_strength,
    "::mlir::UnitAttr":$branching_consequent_monitor,
    "::mlir::UnitAttr":$branching_consequent_nonoverlapped,
    "::mlir::UnitAttr":$branching_consequent_trigger,
    "::mlir::IntegerAttr":$branching_sequence_alternatives,
    "::mlir::UnitAttr":$branching_sequence_monitor,
    "::mlir::UnitAttr":$clocking_event_monitor,
    "::mlir::StringAttr":$clocking_event_monitor_path,
    "::mlir::UnitAttr":$combined_boolean_branching_monitor,
    "::mlir::IntegerAttr":$combined_boolean_branching_pairs,
    "::mlir::IntegerAttr":$combined_boolean_branching_pairs_before_minimization,
    "::mlir::UnitAttr":$combined_bounded_branching_monitor,
    "::mlir::IntegerAttr":$combined_bounded_branching_pairs,
    "::mlir::IntegerAttr":$combined_bounded_branching_pairs_before_minimization,
    "::mlir::UnitAttr":$concurrent_abort_counted,
    "::mlir::StringAttr":$concurrent_action_class,
    "::mlir::UnitAttr":$concurrent_action_control,
    "::mlir::UnitAttr":$concurrent_attempt_action_state,
    "::mlir::UnitAttr":$concurrent_attempt_action_state_load,
    "::mlir::IntegerAttr":$concurrent_attempt_action_state_next,
    "::mlir::UnitAttr":$concurrent_attempt_action_state_storage,
    "::mlir::UnitAttr":$concurrent_attempt_action_state_store,
    "::mlir::UnitAttr":$concurrent_attempt_enable,
    "::mlir::UnitAttr":$concurrent_attempt_start,
    "::mlir::UnitAttr":$concurrent_eos_forced_completion,
    "::mlir::UnitAttr":$concurrent_eos_report,
    "::mlir::UnitAttr":$concurrent_eos_vacuous,
    "::mlir::UnitAttr":$concurrent_kill_epoch,
    "::mlir::UnitAttr":$concurrent_kill_epoch_check,
    "::mlir::UnitAttr":$concurrent_kill_epoch_storage,
    "::mlir::UnitAttr":$concurrent_match_call,
    "::mlir::UnitAttr":$concurrent_match_call_chain,
    "::mlir::UnitAttr":$concurrent_match_call_kill_epoch,
    "::mlir::UnitAttr":$concurrent_report_kill_epoch,
    "::mlir::StringAttr":$consequent_end_of_simulation_strength,
    "::mlir::UnitAttr":$consequent_first_match_equivalence,
    "::mlir::UnitAttr":$cover_sequence_per_match,
    "::mlir::UnitAttr":$default_assertion_failure,
    "::mlir::UnitAttr":$deferred_evaluator,
    "::mlir::BoolAttr":$deferred_result,
    "::mlir::UnitAttr":$empty_antecedent_nonoverlap,
    "::mlir::StringAttr":$end_of_simulation_strength,
    "::mlir::IntegerAttr":$expect_bounded_alternatives,
    "::mlir::UnitAttr":$expect_bounded_branching,
    "::mlir::IntegerAttr":$expect_bounded_horizon,
    "::mlir::IntegerAttr":$expect_bounded_state_words,
    "::mlir::StringAttr":$expect_done_path,
    "::mlir::UnitAttr":$expect_eos_coordinator,
    "::mlir::UnitAttr":$expect_monitor,
    "::mlir::UnitAttr":$expect_monitor_actor,
    "::mlir::IntegerAttr":$expect_one_cycle_alternatives,
    "::mlir::UnitAttr":$expect_one_cycle_branching,
    "::mlir::StringAttr":$expect_operand_strength,
    "::mlir::StringAttr":$expect_result_path,
    "::mlir::UnitAttr":$first_match_monitor,
    "::mlir::UnitAttr":$first_match_priority,
    "::mlir::IntegerAttr":$first_match_priority_groups,
    "::mlir::UnitAttr":$followed_by_monitor,
    "::mlir::UnitAttr":$global_future_monitor,
    "::mlir::UnitAttr":$global_future_resolver,
    "::mlir::UnitAttr":$global_future_wait,
    "::mlir::UnitAttr":$implication_antecedent,
    "::mlir::UnitAttr":$implication_antecedent_failure,
    "::mlir::IntegerAttr":$mixed_empty_antecedent_current_tick_channels,
    "::mlir::IntegerAttr":$mixed_empty_antecedent_handoff_channels,
    "::mlir::UnitAttr":$mixed_empty_antecedent_nonoverlap,
    "::mlir::UnitAttr":$monitor_callback,
    "::mlir::BoolAttr":$monitor_observation_complete,
    "::mlir::UnitAttr":$multiclock_sequence_coordinator,
    "::mlir::UnitAttr":$multiclock_sequence_eos_coordinator,
    "::mlir::StringAttr":$negated_operand_end_of_simulation_strength,
    "::mlir::UnitAttr":$overlapped_empty_matches_ignored,
    "::mlir::UnitAttr":$persistent_antecedent_final_handoff,
    "::mlir::UnitAttr":$persistent_antecedent_handoff,
    "::mlir::IntegerAttr":$persistent_antecedent_handoff_states,
    "::mlir::UnitAttr":$persistent_delay_aggregate_tokens,
    "::mlir::UnitAttr":$persistent_delay_all_matches,
    "::mlir::UnitAttr":$persistent_delay_implication,
    "::mlir::IntegerAttr":$persistent_delay_minimum,
    "::mlir::UnitAttr":$persistent_delay_monitor,
    "::mlir::UnitAttr":$persistent_delay_nonoverlapped,
    "::mlir::IntegerAttr":$persistent_delay_prefix_horizon,
    "::mlir::UnitAttr":$persistent_first_match_equivalence,
    "::mlir::UnitAttr":$persistent_implication_activation,
    "::mlir::UnitAttr":$persistent_implication_antecedent_result,
    "::mlir::UnitAttr":$persistent_implication_handoff,
    "::mlir::UnitAttr":$persistent_monitor,
    "::mlir::UnitAttr":$persistent_repetition_all_matches,
    "::mlir::UnitAttr":$persistent_repetition_antecedent,
    "::mlir::UnitAttr":$persistent_repetition_dfa,
    "::mlir::UnitAttr":$persistent_repetition_implication,
    "::mlir::StringAttr":$persistent_repetition_kind,
    "::mlir::IntegerAttr":$persistent_repetition_max,
    "::mlir::IntegerAttr":$persistent_repetition_min,
    "::mlir::UnitAttr":$persistent_repetition_monitor,
    "::mlir::UnitAttr":$persistent_repetition_nonoverlapped,
    "::mlir::IntegerAttr":$persistent_repetition_states,
    "::mlir::UnitAttr":$persistent_repetition_unbounded,
    "::mlir::UnitAttr":$persistent_unary_aggregate_tokens,
    "::mlir::UnitAttr":$persistent_unary_implication,
    "::mlir::StringAttr":$persistent_unary_kind,
    "::mlir::IntegerAttr":$persistent_unary_minimum,
    "::mlir::UnitAttr":$persistent_unary_monitor,
    "::mlir::UnitAttr":$persistent_unary_nonoverlapped,
    "::mlir::UnitAttr":$persistent_until_aggregate_tokens,
    "::mlir::UnitAttr":$persistent_until_implication,
    "::mlir::UnitAttr":$persistent_until_inclusive,
    "::mlir::StringAttr":$persistent_until_kind,
    "::mlir::UnitAttr":$persistent_until_monitor,
    "::mlir::UnitAttr":$persistent_until_nonoverlapped,
    "::mlir::UnitAttr":$persistent_until_strong,
    "::mlir::UnitAttr":$procedural_assertion_attempt,
    "::mlir::StringAttr":$procedural_assertion_epoch_reference,
    "::mlir::StringAttr":$procedural_assertion_epoch_value,
    "::mlir::StringAttr":$property_abort_action,
    "::mlir::StringAttr":$sample_default_constant,
    "::mlir::UnitAttr":$sample_default_current,
    "::mlir::UnitAttr":$sample_default_dynamic,
    "::mlir::IntegerAttr":$sample_default_snapshot,
    "::mlir::UnitAttr":$sequence_endpoint_default_clock,
    "::mlir::UnitAttr":$sequence_endpoint_event,
    "::mlir::UnitAttr":$sequence_endpoint_monitor,
    "::mlir::StringAttr":$sequence_endpoint_path,
    "::mlir::UnitAttr":$strong_weak_monitor,
    "::mlir::StringAttr":$sva_transition_normal_form,
    "::mlir::UnitAttr":$synchronous_property_abort,
    "::mlir::UnitAttr":$temporal_property_negation,
    "::mlir::UnitAttr":$temporal_property_negation_outside_abort,
    "::mlir::IntegerAttr":$vacuous_sequence_alternatives,

    // Timing, specify paths, nets and UDPs (IEEE 1800-2023 28-32).
    "::mlir::UnitAttr":$continuous_store,
    // Time scaling preparation freezes onto each code unit for unit lowering;
    // finalization removes it with the unit's bindings.
    "::mlir::IntegerAttr":$delay_quantum,
    "::mlir::IntegerAttr":$delay_scale,
    "::mlir::UnitAttr":$delayed_net,
    "::mlir::UnitAttr":$initial_driver_x,
    // A fixed unpacked `interconnect` can acquire a distinct data/net type for
    // every leaf (IEEE 1800-2017 6.6.8). Prepare represents those typeless
    // structural aggregates as independent typed descriptors and freezes the
    // selected descriptor path directly on each semantic leaf expression.
    "::mlir::StringAttr":$interconnect_leaf_path,
    // `#simulation.interconnect_leaf` entries.
    "::mlir::ArrayAttr":$interconnect_leaves,
    "::mlir::SymbolRefAttr":$interconnect_nettype,
    "::obelisk::sim::VPITypeSemanticsAttr":$interconnect_vpi_type,
    "::mlir::DenseI64ArrayAttr":$mos_topology_ids,
    "::mlir::UnitAttr":$negative_timing_adjusted,
    "::mlir::UnitAttr":$negative_timing_delay_commit,
    "::mlir::UnitAttr":$negative_timing_delay_monitor,
    "::mlir::UnitAttr":$negative_timing_transport_activation,
    "::mlir::DenseI64ArrayAttr":$pass_switch_ids,
    "::mlir::DenseI64ArrayAttr":$propagation_delays,
    "::mlir::StringAttr":$resolution_function_path,
    "::mlir::SymbolRefAttr":$resolution_function_symbol,
    "::mlir::UnitAttr":$skew_deadline_helper,
    "::mlir::IntegerAttr":$strength_bank,
    "::mlir::IntegerAttr":$strength_driver_bank,
    "::mlir::IntegerAttr":$strength_group,
    "::mlir::DenseI64ArrayAttr":$timing_adjusted_ticks,
    "::mlir::DenseI64ArrayAttr":$timing_check_arg_ticks,
    "::mlir::UnitAttr":$timing_check_coordinator,
    "::mlir::DenseI64ArrayAttr":$timing_delayed_source_delays,
    "::mlir::DenseI64ArrayAttr":$timing_delayed_storage_ids,
    "::mlir::DenseI64ArrayAttr":$timing_delayed_terminal_indices,
    // `#simulation.timing_path_monitor` and `#simulation.timing_path_rule`
    // entries, with delays in design precision ticks.
    "::mlir::ArrayAttr":$timing_path_monitor_rules,
    "::mlir::ArrayAttr":$timing_path_rules,
    "::mlir::FlatSymbolRefAttr":$timing_timer_helper,
    "::mlir::IntegerAttr":$timing_timer_storage,
    // The `#simulation.udp` declaration of a primitive instance's unit.
    "::obelisk::sim::UdpAttr":$udp_metadata,
    "::mlir::UnitAttr":$user_defined_net,
    "::mlir::UnitAttr":$user_net_driver,
    "::mlir::UnitAttr":$user_net_raw_drive,

    // Calls, classes, captures, processes and initializers.
    "::mlir::UnitAttr":$aggregate_member_initializers,
    "::mlir::UnitAttr":$automatic_reference_capture,
    // Transient function attribute containing ArgumentBindingAttr,
    // LocalBindingAttr, and ConstantBindingAttr entries, frozen by preparation
    // for unit lowering and removed by finalization.
    "::mlir::ArrayAttr":$bindings,
    "::mlir::FlatSymbolRefAttr":$callee,
    "::mlir::ArrayAttr":$callee_captures,
    // `#simulation.callee_formal` entries, one per callee formal.
    "::mlir::ArrayAttr":$callee_formals,
    "::mlir::ArrayAttr":$callee_read_captures,
    "::mlir::ArrayAttr":$callee_written_captures,
    "::obelisk::sim::CaptureKindAttr":$capture_kind,
    // Marks an explicit class-containing bit-stream conversion whose source is
    // exactly the enclosing method's current-instance `this`. This exception is
    // semantic: aliases and handles reached through another expression never
    // inherit it.
    "::mlir::UnitAttr":$class_bitstream_allow_hidden_root,
    "::mlir::IntegerAttr":$class_bitstream_bytecode_function,
    "::mlir::IntegerAttr":$class_bitstream_bytecode_site,
    // Marks an executable class field that corresponds to a source-declared
    // instance property. Compiler-owned fields and static properties are absent
    // from the object bit-stream inventory.
    "::mlir::UnitAttr":$class_bitstream_member,
    // Marks the instance field holding an embedded covergroup's handle. A
    // shallow copy sets it to null in the new object (IEEE 1800-2023 8.12).
    "::mlir::UnitAttr":$embedded_covergroup,
    // Marks a code unit for a checker procedure or continuous assignment
    // (IEEE 1800-2023 17.5, 17.7.3). It runs in Reactive, and a checker
    // always_ff reads sampled values outside its event control.
    "::mlir::UnitAttr":$checker_procedure,
    // Module marker set by preparation when source classes may be bitstreamed;
    // it keeps whole-design class planning off ordinary compilation paths.
    "::mlir::UnitAttr":$class_bitstream_source,
    // Source member visibility retained for recursive class bit-stream
    // legality.
    "::obelisk::sim::MemberVisibilityAttr":$class_bitstream_visibility,
    "::mlir::IntegerAttr":$class_bitstream_width,
    "::mlir::FlatSymbolRefAttr":$class_field,
    "::mlir::UnitAttr":$class_instance,
    "::mlir::FlatSymbolRefAttr":$class_method,
    "::mlir::IntegerAttr":$class_signature,
    "::mlir::IntegerAttr":$class_slot,
    "::mlir::UnitAttr":$class_super,
    "::mlir::UnitAttr":$class_virtual,
    "::mlir::StringAttr":$constant_value,
    "::mlir::UnitAttr":$constructor,
    "::mlir::IntegerAttr":$control_target_id,
    "::mlir::UnitAttr":$covergroup_block_event_sample_evaluator,
    "::mlir::UnitAttr":$covergroup_event_sample_evaluator,
    "::mlir::UnitAttr":$covergroup_strobe_sample_evaluator,
    // Marks an event storage cell whose declaration initializer supplies the
    // initial handle. The root initializer must not first create an unused
    // synchronization object for such a cell (IEEE 1800-2017 6.17).
    "::mlir::UnitAttr":$event_explicit_initializer,
    // Marks a foreach iterator that must remain addressable while an outlined
    // observer reevaluates an expression from the loop body.
    "::mlir::UnitAttr":$foreach_iterator_observer_capture,
    "::mlir::IntegerAttr":$fork_code_unit_id,
    "::mlir::FlatSymbolRefAttr":$initialize_field,
    "::mlir::StringAttr":$initialize_net,
    "::mlir::IntegerAttr":$initialize_sample_default,
    "::mlir::StringAttr":$initialize_static,
    "::mlir::IntegerAttr":$initialize_subelement,
    "::obelisk::sim::CovergroupInstanceOptionKindAttr":$initialize_type_option_kind,
    // Connect a one-time static option initializer to its typed coverage owner.
    "::mlir::SymbolRefAttr":$initialize_type_option_owner,
    "::mlir::UnitAttr":$is_task,
    "::mlir::UnitAttr":$lowered,
    "::mlir::IntegerAttr":$nba_event_code_unit_id,
    "::mlir::StringAttr":$nba_event_hierarchy,
    "::mlir::FlatSymbolRefAttr":$observer,
    "::mlir::ArrayAttr":$observer_captures,
    // Optional packed/real result type imposed by the context that requested an
    // observer. Procedural continuous assignments use the target's assignment
    // type so every reevaluation has exactly the same conversion as the initial
    // statement execution.
    "::mlir::TypeAttr":$observer_coerced_type,
    "::mlir::ArrayAttr":$observer_dependencies,
    "::mlir::IntegerAttr":$observer_result,
    "::mlir::UnitAttr":$placeholder,
    // Marks a Simulation-dialect property-initializer fragment deliberately
    // nested in semantic constructor syntax so unit lowering preserves its
    // position immediately after an explicit super.new call.
    "::mlir::UnitAttr":$prepared_initializer,
    "::mlir::UnitAttr":$resumable_control_target,
    // Hierarchical path of the variable that holds a function's return value.
    "::mlir::StringAttr":$return_variable_path,
    "::mlir::UnitAttr":$sampled_observer,
    "::mlir::UnitAttr":$static_class_property,
    "::mlir::UnitAttr":$static_class_receiver,
    "::mlir::UnitAttr":$static_initializer,
    "::mlir::StringAttr":$static_net_constant,
    "::mlir::IntegerAttr":$static_site_id,
    // Marks a storage descriptor that a subroutine owns. Its writers are the
    // subroutine's callers rather than drivers of a design variable.
    "::mlir::UnitAttr":$subroutine_storage,
    "::mlir::IntegerAttr":$this_argument,
    // A point/cross selection qualifies static storage; it is not a value
    // field.
    "::mlir::UnitAttr":$type_option_item_qualifier,
    // `#simulation.virtual_interface_callee` entries.
    "::mlir::ArrayAttr":$virtual_interface_callees,
    // `#simulation.virtual_interface_event` entries of an interface scope.
    "::mlir::ArrayAttr":$virtual_interface_clock_events,
    "::mlir::StringAttr":$virtual_interface_clocking_output,
    "::mlir::StringAttr":$virtual_interface_member,
    // Names the interface member whose elaborated instance owns a child scope.
    // This avoids recovering escaped or indexed identifiers from display paths
    // when a virtual-interface member selects another interface instance.
    "::mlir::StringAttr":$virtual_interface_parent_member,
    // Ordered nested-interface selections omitted from a flattened semantic
    // call receiver, as `#simulation.virtual_interface_member` entries.
    "::mlir::ArrayAttr":$virtual_interface_receiver_members,
    "::mlir::UnitAttr":$void_function,

    // Descriptors, hierarchy and VPI identity.

    // An exclusive slot whose initializer can be recreated at every
    // activation: mutable lanes are defined before reads, and all other lanes
    // keep defaults.
    "::mlir::UnitAttr":$activation_default_slot,
    // Frozen by the bytecode encoder from complete call-closure effect
    // analysis. Recomputed whenever the bytecode image is rebuilt.
    "::mlir::UnitAttr":$bytecode_read_only,
    "::mlir::TypeAttr":$descriptor_aggregate_type,
    "::mlir::IntegerAttr":$descriptor_id,
    "::mlir::DenseI64ArrayAttr":$descriptor_indices,
    "::mlir::IntegerAttr":$descriptor_low,
    "::mlir::IntegerAttr":$descriptor_packed_low,
    "::mlir::TypeAttr":$descriptor_root_type,
    // Marks a process-lifetime `simulation.ref.alloc` copy of an exclusive root
    // that is flushed back to that root at every suspension and return. Its
    // contents at any resumption equal a fresh load of the root.
    "::mlir::UnitAttr":$flushed_private_slot,
    // Marks a call into compiled code that cannot reach a GC poll.
    "::mlir::UnitAttr":$gc_poll_free,
    "::mlir::StringAttr":$hierarchical_name,
    "::mlir::UnitAttr":$rematerialized,
    "::mlir::FlatSymbolRefAttr":$vpi_anchor,
    "::mlir::BoolAttr":$vpi_automatic,
    "::mlir::BoolAttr":$vpi_cell_instance,
    "::mlir::StringAttr":$vpi_definition_name,
    // FlatSymbolRefAttr naming the immutable relation-backed anchor that owns
    // the public VPI identity of executable named-event-array storage. The
    // storage remains in the execution layout but is omitted from the VPI
    // object inventory.
    "::mlir::FlatSymbolRefAttr":$vpi_identity_delegated,
    // Exact frontend enum identity retained solely to reconnect a value object
    // to its anonymous enum typespec in the immutable VPI relation image.
    "::mlir::IntegerAttr":$vpi_source_type_identity,
    "::mlir::BoolAttr":$vpi_top,

    // Enumeration methods, dynamic casts and $dumpports.
    "::mlir::UnitAttr":$dumpports_scope,
    "::mlir::ArrayAttr":$dynamic_cast_enum_values,
    "::mlir::UnitAttr":$dynamic_cast_task,
    "::mlir::ArrayAttr":$enum_format_names,
    "::mlir::ArrayAttr":$enum_format_values,
    "::mlir::ArrayAttr":$enum_method_names,
    "::mlir::ArrayAttr":$enum_method_values
  );
}

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONDIALECT_TD
