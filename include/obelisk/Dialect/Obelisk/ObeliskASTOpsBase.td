//===- ObeliskASTOpsBase.td - High-level SV operation schemas -*- tablegen -*-===//

#ifndef OBELISK_DIALECT_OBELISK_OBELISKASTOPSBASE_TD
#define OBELISK_DIALECT_OBELISK_OBELISKASTOPSBASE_TD

include "obelisk/Dialect/Obelisk/ObeliskDialect.td"
include "obelisk/Dialect/Obelisk/ObeliskEnums.td"
include "obelisk/Dialect/Obelisk/ObeliskTypes.td"
include "mlir/IR/OpBase.td"
include "mlir/IR/SymbolInterfaces.td"

def ObeliskSemanticASTNode : NativeOpTrait<"SemanticASTNode">;
def ObeliskSemanticDeclarativeNode
    : NativeOpTrait<"SemanticDeclarativeNode">;
def ObeliskRejectInvalidSemanticNode
    : NativeOpTrait<"RejectInvalidSemanticNode">;
def ObeliskVerifySemanticReferenceGraph
    : NativeOpTrait<"VerifySemanticReferenceGraph">;
def ObeliskVerifyRepetitionMetadata
    : NativeOpTrait<"VerifyRepetitionMetadata">;
def ObeliskVerifySequenceRangeMetadata
    : NativeOpTrait<"VerifySequenceRangeMetadata">;

class ObeliskASTNodeOp<string mnemonic, list<Trait> traits = [],
                       dag semanticArgs = (ins)>
    : Op<ObeliskDialect, mnemonic,
         !listconcat([NoTerminator, SingleBlock, ObeliskSemanticASTNode],
                     traits)> {
  let summary = "Concrete high-level elaborated SystemVerilog operation";
  let arguments = !con((ins
    I64Attr:$node_id,
    OptionalAttr<TypeAttr>:$source_range,
    OptionalAttr<TypeAttr>:$original_source_range,
    OptionalAttr<DictArrayAttr>:$macro_expansion_stack,
    OptionalAttr<StrAttr>:$source_file,
    OptionalAttr<I64Attr>:$source_end_line,
    OptionalAttr<I64Attr>:$source_end_column,
    OptionalAttr<StrAttr>:$macro_name
  ), semanticArgs);
  let regions = (region AnyRegion:$body);
  let assemblyFormat = "attr-dict-with-keyword $body";
}

class ObeliskInvalidASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [ObeliskRejectInvalidSemanticNode]>;

class ObeliskSymbolASTNodeOp<string mnemonic, list<Trait> traits = [],
                             dag symbolArgs = (ins)>
    : ObeliskASTNodeOp<mnemonic, !listconcat([Symbol], traits),
        !con((ins
          StrAttr:$sym_name,
          OptionalAttr<StrAttr>:$name,
          OptionalAttr<StrAttr>:$hierarchical_name,
          OptionalAttr<TypeAttr>:$semantic_type
        ), symbolArgs)>;

// Every semantic scope is a symbol table and is isolated from above. The AST is
// a pure attribute-and-region tree with no SSA values, so isolation is always
// satisfied; declaring it lets MLIR verify and schedule analysis and
// optimization passes over each scope in parallel. Cross-scope references are
// symbolic (SymbolRefAttr), so they legitimately cross the isolation boundary
// through the symbol mechanism rather than through captured SSA values.
class ObeliskSymbolTableASTNodeOp<string mnemonic, dag symbolArgs = (ins),
                                 list<Trait> traits = []>
    : ObeliskSymbolASTNodeOp<mnemonic,
        !listconcat([SymbolTable, IsolatedFromAbove], traits),
        symbolArgs>;

class ObeliskRootASTNodeOp<string mnemonic>
    : ObeliskSymbolASTNodeOp<mnemonic,
        [SymbolTable, IsolatedFromAbove,
         ObeliskVerifySemanticReferenceGraph]>;

class ObeliskConstantSymbolASTNodeOp<string mnemonic>
    : ObeliskSymbolASTNodeOp<mnemonic, [], (ins StrAttr:$constant_value)>;

class ObeliskDirectedSymbolASTNodeOp<string mnemonic>
    : ObeliskSymbolASTNodeOp<mnemonic, [],
        (ins ObeliskSVArgumentDirection:$direction)>;

class ObeliskDefinitionASTNodeOp<string mnemonic>
    : ObeliskSymbolTableASTNodeOp<mnemonic,
        (ins ObeliskSVDefinitionKind:$definition_kind)>;

class ObeliskProceduralBlockASTNodeOp<string mnemonic>
    : ObeliskSymbolASTNodeOp<mnemonic, [],
        (ins ObeliskSVProceduralBlockKind:$procedure_kind)>;

class ObeliskStatementBlockASTNodeOp<string mnemonic>
    : ObeliskSymbolTableASTNodeOp<mnemonic,
        (ins ObeliskSVStatementBlockKind:$block_kind)>;

class ObeliskSubroutineASTNodeOp<string mnemonic>
    : ObeliskSymbolTableASTNodeOp<mnemonic, (ins
        ObeliskSVSubroutineKind:$subroutine_kind,
        DefaultValuedAttr<ObeliskSVVariableLifetime,
          "::obelisk::ir::SVVariableLifetime::Automatic">:$default_lifetime,
        DefaultValuedAttr<ObeliskSVVisibility,
          "::obelisk::ir::SVVisibility::Public">:$member_visibility,
        OptionalAttr<UnitAttr>:$is_virtual,
        OptionalAttr<UnitAttr>:$is_declared_virtual,
        OptionalAttr<UnitAttr>:$is_pure,
        OptionalAttr<UnitAttr>:$is_static,
        OptionalAttr<UnitAttr>:$is_constructor,
        OptionalAttr<UnitAttr>:$is_interface_extern,
        OptionalAttr<UnitAttr>:$is_modport_import,
        OptionalAttr<UnitAttr>:$is_modport_export,
        OptionalAttr<UnitAttr>:$is_dpi_import,
        OptionalAttr<UnitAttr>:$is_dpi_context,
        OptionalAttr<StrAttr>:$dpi_c_identifier,
        OptionalAttr<StrAttr>:$dpi_export_c_identifier,
        OptionalAttr<UnitAttr>:$is_builtin,
        OptionalAttr<UnitAttr>:$is_randomize,
        OptionalAttr<UnitAttr>:$is_fork_join,
        OptionalAttr<UnitAttr>:$has_defaulted_super_arg,
        OptionalAttr<UnitAttr>:$is_initial,
        OptionalAttr<UnitAttr>:$is_extends,
        OptionalAttr<UnitAttr>:$is_final,
        OptionalAttr<UnitAttr>:$is_pre_post_randomize,
        DefaultValuedAttr<I64Attr, "0">:$out_of_block_index,
        OptionalAttr<SymbolRefAttr>:$override_symbol,
        OptionalAttr<StrAttr>:$override_path,
        OptionalAttr<SymbolRefAttr>:$prototype_symbol,
        OptionalAttr<StrAttr>:$prototype_path,
        OptionalAttr<SymbolRefAttr>:$return_variable_symbol,
        OptionalAttr<StrAttr>:$return_variable_path,
        OptionalAttr<SymbolRefAttr>:$this_variable_symbol,
        OptionalAttr<StrAttr>:$this_variable_path
      )>;

class ObeliskMethodPrototypeASTNodeOp<string mnemonic>
    : ObeliskSymbolTableASTNodeOp<mnemonic, (ins
        ObeliskSVSubroutineKind:$subroutine_kind,
        DefaultValuedAttr<ObeliskSVVisibility,
          "::obelisk::ir::SVVisibility::Public">:$member_visibility,
        OptionalAttr<UnitAttr>:$is_virtual,
        OptionalAttr<UnitAttr>:$is_declared_virtual,
        OptionalAttr<UnitAttr>:$is_pure,
        OptionalAttr<UnitAttr>:$is_static,
        OptionalAttr<UnitAttr>:$is_constructor,
        OptionalAttr<UnitAttr>:$is_interface_extern,
        OptionalAttr<UnitAttr>:$is_modport_import,
        OptionalAttr<UnitAttr>:$is_modport_export,
        OptionalAttr<UnitAttr>:$is_dpi_import,
        OptionalAttr<UnitAttr>:$is_dpi_context,
        OptionalAttr<StrAttr>:$dpi_c_identifier,
        OptionalAttr<StrAttr>:$dpi_export_c_identifier,
        OptionalAttr<UnitAttr>:$is_builtin,
        OptionalAttr<UnitAttr>:$is_fork_join,
        OptionalAttr<UnitAttr>:$has_defaulted_super_arg,
        OptionalAttr<UnitAttr>:$is_initial,
        OptionalAttr<UnitAttr>:$is_extends,
        OptionalAttr<UnitAttr>:$is_final,
        OptionalAttr<SymbolRefAttr>:$subroutine_symbol,
        OptionalAttr<StrAttr>:$subroutine_path,
        OptionalAttr<SymbolRefAttr>:$override_symbol,
        OptionalAttr<StrAttr>:$override_path,
        I64Attr:$extern_implementation_count,
        SymbolRefArrayAttr:$extern_implementation_symbols,
        StrArrayAttr:$extern_implementation_paths
      ), [ObeliskSemanticDeclarativeNode]>;

class ObeliskVariableSymbolASTNodeOp<string mnemonic,
                                     dag variableArgs = (ins)>
    : ObeliskSymbolASTNodeOp<mnemonic, [], !con((ins
        DefaultValuedAttr<ObeliskSVVariableLifetime,
          "::obelisk::ir::SVVariableLifetime::Automatic">:$lifetime,
        DefaultValuedAttr<ObeliskSVRandMode,
          "::obelisk::ir::SVRandMode::None">:$rand_mode,
        OptionalAttr<UnitAttr>:$is_const,
        OptionalAttr<UnitAttr>:$is_compiler_generated,
        OptionalAttr<UnitAttr>:$is_immutable_coverage_option,
        OptionalAttr<UnitAttr>:$is_coverage_sample_formal,
        OptionalAttr<UnitAttr>:$is_checker_free_variable,
        OptionalAttr<UnitAttr>:$is_ref_static
      ), variableArgs)>;

class ObeliskClassPropertyASTNodeOp<string mnemonic>
    : ObeliskVariableSymbolASTNodeOp<mnemonic,
        (ins DefaultValuedAttr<ObeliskSVVisibility,
          "::obelisk::ir::SVVisibility::Public">:$member_visibility)>;

class ObeliskFormalArgumentASTNodeOp<string mnemonic>
    : ObeliskVariableSymbolASTNodeOp<mnemonic, (ins
        ObeliskSVArgumentDirection:$direction,
        OptionalAttr<SymbolRefAttr>:$merged_variable_symbol,
        OptionalAttr<StrAttr>:$merged_variable_path
      )>;

class ObeliskIteratorASTNodeOp<string mnemonic>
    : ObeliskVariableSymbolASTNodeOp<mnemonic, (ins
        TypeAttr:$array_type,
        StrAttr:$index_method_name
      )>;

class ObeliskClockVarASTNodeOp<string mnemonic>
    : ObeliskVariableSymbolASTNodeOp<mnemonic, (ins
        ObeliskSVArgumentDirection:$direction,
        EdgeKind:$input_edge,
        EdgeKind:$output_edge,
        BoolAttr:$has_input_delay,
        BoolAttr:$has_output_delay
      )>;

class ObeliskLocalAssertionVarASTNodeOp<string mnemonic>
    : ObeliskVariableSymbolASTNodeOp<mnemonic, (ins
        OptionalAttr<SymbolRefAttr>:$formal_port_symbol,
        OptionalAttr<StrAttr>:$formal_port_path
      )>;

class ObeliskFieldASTNodeOp<string mnemonic>
    : ObeliskVariableSymbolASTNodeOp<mnemonic, (ins
        I64Attr:$bit_offset,
        I64Attr:$field_index
      )>;

class ObeliskConstraintBlockASTNodeOp<string mnemonic>
    : ObeliskSymbolTableASTNodeOp<mnemonic, (ins
        OptionalAttr<UnitAttr>:$is_pure,
        OptionalAttr<UnitAttr>:$is_static,
        OptionalAttr<UnitAttr>:$is_extern,
        OptionalAttr<UnitAttr>:$is_explicit_extern,
        OptionalAttr<UnitAttr>:$is_initial,
        OptionalAttr<UnitAttr>:$is_extends,
        OptionalAttr<UnitAttr>:$is_final,
        DefaultValuedAttr<I64Attr, "0">:$out_of_block_index,
        OptionalAttr<SymbolRefAttr>:$this_variable_symbol,
        OptionalAttr<StrAttr>:$this_variable_path
      ), [ObeliskSemanticDeclarativeNode]>;

class ObeliskGenericClassDefASTNodeOp<string mnemonic>
    : ObeliskSymbolTableASTNodeOp<mnemonic, (ins
        BoolAttr:$is_interface,
        I64Attr:$specialization_count,
        OptionalAttr<SymbolRefAttr>:$first_forward_declaration_symbol,
        OptionalAttr<StrAttr>:$first_forward_declaration_path
      ), [ObeliskSemanticDeclarativeNode]>;

class ObeliskInstanceASTNodeOp<string mnemonic>
    : ObeliskSymbolTableASTNodeOp<mnemonic, (ins
        OptionalAttr<SymbolRefAttr>:$referenced_symbol,
        OptionalAttr<StrAttr>:$referenced_path,
        OptionalAttr<BoolAttr>:$is_uninstantiated
      )>;

def SVPortConnectionOp : Op<ObeliskDialect, "sv.port.connection", [
    NoTerminator, ObeliskSemanticASTNode
  ]> {
  let summary = "one resolved scalar elaborated port connection";
  let arguments = (ins
    I64Attr:$node_id,
    SymbolRefAttr:$formal_symbol,
    StrAttr:$formal_path,
    I64Attr:$formal_ordinal,
    OptionalAttr<StrAttr>:$formal_name,
    ObeliskSVArgumentDirection:$direction,
    TypeAttr:$formal_type,
    BoolAttr:$is_net,
    BoolAttr:$is_ansi,
    BoolAttr:$actual_is_constant,
    ObeliskSVPortConnectionKind:$provenance,
    OptionalAttr<SymbolRefAttr>:$internal_symbol,
    OptionalAttr<StrAttr>:$internal_path,
    OptionalAttr<SymbolRefAttr>:$interface_instance_symbol,
    OptionalAttr<StrAttr>:$interface_instance_path,
    OptionalAttr<DenseI64ArrayAttr>:$interface_shape,
    OptionalAttr<StrAttr>:$selected_modport,
    OptionalAttr<StrAttr>:$unsupported_strength,
    OptionalAttr<StrAttr>:$unsupported_delay
  );
  let regions = (region AnyRegion:$internal, AnyRegion:$actual);
  let assemblyFormat = "attr-dict-with-keyword $internal $actual";
}

class ObeliskContinuousAssignASTNodeOp<string mnemonic>
    : ObeliskSymbolASTNodeOp<mnemonic, [], (ins
        OptionalAttr<StrAttr>:$unsupported_strength,
        OptionalAttr<StrAttr>:$unsupported_delay
      )>;

class ObeliskNetSymbolASTNodeOp<string mnemonic>
    : ObeliskSymbolASTNodeOp<mnemonic, [], (ins
        ObeliskSVNetKind:$net_kind,
        BoolAttr:$is_implicit,
        OptionalAttr<StrAttr>:$unsupported_strength,
        OptionalAttr<StrAttr>:$unsupported_delay
      )>;

class ObeliskTypeASTNodeOp<string mnemonic, list<Trait> traits = [],
                           dag typeArgs = (ins)>
    : ObeliskASTNodeOp<mnemonic, !listconcat([Symbol], traits),
        !con((ins
          StrAttr:$sym_name,
          OptionalAttr<StrAttr>:$name,
          OptionalAttr<StrAttr>:$hierarchical_name,
          TypeAttr:$semantic_type
        ), typeArgs)>;

class ObeliskScopeTypeASTNodeOp<string mnemonic, dag typeArgs = (ins),
                               list<Trait> traits = []>
    : ObeliskTypeASTNodeOp<mnemonic,
        !listconcat([SymbolTable, IsolatedFromAbove], traits), typeArgs>;

class ObeliskNetTypeASTNodeOp<string mnemonic>
    : ObeliskTypeASTNodeOp<mnemonic, [], (ins
        ObeliskSVNetKind:$net_kind,
        TypeAttr:$data_type,
        BoolAttr:$is_builtin,
        OptionalAttr<SymbolRefAttr>:$resolution_function_symbol,
        OptionalAttr<StrAttr>:$resolution_function_path
      )>;

class ObeliskClassTypeASTNodeOp<string mnemonic>
    : ObeliskScopeTypeASTNodeOp<mnemonic, (ins
        BoolAttr:$is_abstract,
        BoolAttr:$is_interface,
        BoolAttr:$is_final,
        BoolAttr:$is_uninstantiated,
        OptionalAttr<TypeAttr>:$base_class,
        TypeArrayAttr:$implemented_interfaces,
        TypeArrayAttr:$declared_interfaces,
        OptionalAttr<SymbolRefAttr>:$generic_class_symbol,
        OptionalAttr<StrAttr>:$generic_class_path,
        SymbolRefArrayAttr:$generic_parameter_symbols,
        StrArrayAttr:$generic_parameter_paths,
        OptionalAttr<SymbolRefAttr>:$this_variable_symbol,
        OptionalAttr<StrAttr>:$this_variable_path,
        OptionalAttr<SymbolRefAttr>:$constructor_symbol,
        OptionalAttr<StrAttr>:$constructor_path,
        BoolAttr:$has_base_constructor_call,
        I64Attr:$bitstream_width,
        BoolAttr:$has_cycles
      ), [ObeliskSemanticDeclarativeNode]>;

class ObeliskCovergroupTypeASTNodeOp<string mnemonic>
    : ObeliskScopeTypeASTNodeOp<mnemonic,
        (ins OptionalAttr<TypeAttr>:$base_group),
        [ObeliskSemanticDeclarativeNode]>;

class ObeliskCoverageBinASTNodeOp<string mnemonic>
    : ObeliskSymbolASTNodeOp<mnemonic, [ObeliskSemanticDeclarativeNode], (ins
        ObeliskSVCoverageBinKind:$bins_kind,
        BoolAttr:$is_array,
        BoolAttr:$is_wildcard,
        BoolAttr:$is_default,
        BoolAttr:$is_default_sequence
      )>;

class ObeliskExpressionASTNodeOp<string mnemonic, dag expressionArgs = (ins)>
    : ObeliskASTNodeOp<mnemonic, [],
        !con((ins TypeAttr:$semantic_type), expressionArgs)>;

class ObeliskConstantExpressionASTNodeOp<string mnemonic>
    : ObeliskExpressionASTNodeOp<mnemonic, (ins StrAttr:$constant_value)>;

class ObeliskTimeLiteralASTNodeOp<string mnemonic>
    : ObeliskExpressionASTNodeOp<mnemonic,
        (ins StrAttr:$constant_value, StrAttr:$time_scale)>;

class ObeliskReferenceExpressionASTNodeOp<string mnemonic>
    : ObeliskExpressionASTNodeOp<mnemonic, (ins
        SymbolRefAttr:$referenced_symbol,
        StrAttr:$referenced_path
      )>;

class ObeliskCallExpressionASTNodeOp<string mnemonic>
    : ObeliskExpressionASTNodeOp<mnemonic, (ins
        StrAttr:$callee_name,
        BoolAttr:$is_system_call,
        ObeliskSVSubroutineKind:$subroutine_kind,
        I64Attr:$argument_count,
        BoolAttr:$has_this_class,
        BoolAttr:$is_super_class,
        BoolAttr:$has_output_arguments,
        OptionalAttr<SymbolRefAttr>:$referenced_symbol,
        OptionalAttr<StrAttr>:$referenced_path,
        OptionalAttr<SymbolRefAttr>:$system_scope_symbol,
        OptionalAttr<StrAttr>:$system_scope_path,
        OptionalAttr<StrAttr>:$system_library_cell,
        OptionalAttr<SymbolRefAttr>:$iterator_variable_symbol,
        OptionalAttr<StrAttr>:$iterator_variable_path,
        BoolAttr:$has_iterator_expression,
        BoolAttr:$has_inline_constraints,
        ArrayAttr:$constraint_restrictions
      )>;

class ObeliskNewClassExpressionASTNodeOp<string mnemonic>
    : ObeliskExpressionASTNodeOp<mnemonic, (ins
        BoolAttr:$is_super_class
      )>;

class ObeliskUnaryExpressionASTNodeOp<string mnemonic>
    : ObeliskExpressionASTNodeOp<mnemonic,
        (ins ObeliskSVUnaryOperator:$operator_kind)>;

class ObeliskBinaryExpressionASTNodeOp<string mnemonic>
    : ObeliskExpressionASTNodeOp<mnemonic,
        (ins ObeliskSVBinaryOperator:$operator_kind)>;

class ObeliskAssignmentExpressionASTNodeOp<string mnemonic>
    : ObeliskExpressionASTNodeOp<mnemonic, (ins
        ObeliskSVAssignmentKind:$assignment_kind,
        DefaultValuedAttr<BoolAttr, "false">:$has_timing_control,
        OptionalAttr<ObeliskSVBinaryOperator>:$operator_kind
      )>;

class ObeliskRangeSelectExpressionASTNodeOp<string mnemonic>
    : ObeliskExpressionASTNodeOp<mnemonic,
        (ins ObeliskSVRangeSelectionKind:$selection_kind)>;

class ObeliskStatementASTNodeOp<string mnemonic> : ObeliskASTNodeOp<mnemonic>;

class ObeliskForLoopStatementASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [], (ins
        I64Attr:$initializer_count,
        BoolAttr:$has_condition,
        I64Attr:$step_count
      )> {
  let hasVerifier = 1;
}

class ObeliskForeachLoopStatementASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [], (ins
        DictArrayAttr:$loop_dimensions
      )> {
  let hasVerifier = 1;
}

class ObeliskBlockStatementASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [], (ins
        DefaultValuedAttr<ObeliskSVStatementBlockKind,
          "::obelisk::ir::SVStatementBlockKind::Sequential">:$block_kind,
        OptionalAttr<SymbolRefAttr>:$block_symbol,
        OptionalAttr<StrAttr>:$block_path
      )>;

class ObeliskDisableStatementASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [], (ins
        OptionalAttr<SymbolRefAttr>:$target_symbol,
        OptionalAttr<StrAttr>:$target_path,
        DefaultValuedAttr<BoolAttr, "false">:$is_hierarchical
      )>;

class ObeliskVariableDeclarationStatementASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [], (ins
        SymbolRefAttr:$referenced_symbol,
        StrAttr:$referenced_path
      )>;

class ObeliskWaitOrderStatementASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [], (ins
        DefaultValuedAttr<I64Attr, "0">:$event_count,
        DefaultValuedAttr<BoolAttr, "false">:$has_success_action,
        DefaultValuedAttr<BoolAttr, "false">:$has_failure_action
      )>;

class ObeliskEventTriggerStatementASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [], (ins
        DefaultValuedAttr<BoolAttr, "false">:$is_nonblocking,
        DefaultValuedAttr<BoolAttr, "false">:$has_timing_control
      )>;

class ObeliskConditionalStatementASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [], (ins
        ObeliskSVUniquePriorityCheck:$check_kind,
        I64Attr:$condition_count,
        DenseI64ArrayAttr:$condition_pattern_flags,
        BoolAttr:$has_else
      )> {
  let hasVerifier = 1;
}

class ObeliskCaseStatementASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [], (ins
        ObeliskSVCaseCondition:$condition_kind,
        ObeliskSVUniquePriorityCheck:$check_kind,
        I64Attr:$item_count,
        DenseI64ArrayAttr:$item_label_counts,
        BoolAttr:$has_default
      )> {
  let hasVerifier = 1;
}

class ObeliskPatternCaseStatementASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [], (ins
        ObeliskSVCaseCondition:$condition_kind,
        ObeliskSVUniquePriorityCheck:$check_kind,
        I64Attr:$item_count,
        DenseI64ArrayAttr:$item_filter_flags,
        BoolAttr:$has_default
      )> {
  let hasVerifier = 1;
}

class ObeliskInsideExpressionASTNodeOp<string mnemonic>
    : ObeliskExpressionASTNodeOp<mnemonic, (ins I64Attr:$item_count)> {
  let hasVerifier = 1;
}

class ObeliskValueRangeExpressionASTNodeOp<string mnemonic>
    : ObeliskExpressionASTNodeOp<mnemonic,
        (ins ObeliskSVValueRangeKind:$range_kind)> {
  let hasVerifier = 1;
}

class ObeliskImmediateAssertionStatementASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [ObeliskSemanticDeclarativeNode], (ins
        ObeliskSVAssertionKind:$assertion_kind,
        BoolAttr:$is_deferred,
        BoolAttr:$is_final,
        BoolAttr:$has_pass_action,
        BoolAttr:$has_fail_action
      )>;

class ObeliskConcurrentAssertionStatementASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [ObeliskSemanticDeclarativeNode], (ins
        ObeliskSVAssertionKind:$assertion_kind,
        BoolAttr:$has_pass_action,
        BoolAttr:$has_fail_action
      )>;

class ObeliskProceduralAssignStatementASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [], (ins BoolAttr:$is_force)>;
class ObeliskTimingASTNodeOp<string mnemonic> : ObeliskASTNodeOp<mnemonic>;

class ObeliskDelay3TimingASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [], (ins I64Attr:$delay_count)>;

class ObeliskSignalEventTimingASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [], (ins
        EdgeKind:$edge_kind,
        BoolAttr:$has_iff
      )>;

class ObeliskEventListTimingASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [], (ins I64Attr:$event_count)>;

class ObeliskBlockEventListTimingASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [], (ins ArrayAttr:$event_is_begin)>;
class ObeliskConstraintASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [ObeliskSemanticDeclarativeNode]>;

class ObeliskConstraintListASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [ObeliskSemanticDeclarativeNode], (ins I64Attr:$item_count)>;

class ObeliskExpressionConstraintASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [ObeliskSemanticDeclarativeNode], (ins BoolAttr:$is_soft)>;

class ObeliskConditionalConstraintASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [ObeliskSemanticDeclarativeNode], (ins BoolAttr:$has_else)>;

class ObeliskCountedConstraintASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [ObeliskSemanticDeclarativeNode], (ins I64Attr:$item_count)>;

class ObeliskSolveBeforeConstraintASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [ObeliskSemanticDeclarativeNode], (ins
        I64Attr:$solve_count,
        I64Attr:$after_count
      )>;

class ObeliskForeachConstraintASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [ObeliskSemanticDeclarativeNode], (ins DictArrayAttr:$loop_dimensions)>;

class ObeliskAssertionASTNodeOp<string mnemonic, dag assertionArgs = (ins),
                                list<Trait> traits = []>
    : ObeliskASTNodeOp<mnemonic,
        !listconcat([ObeliskSemanticDeclarativeNode], traits), !con(
        (ins OptionalAttr<TypeAttr>:$semantic_type), assertionArgs)>;

class ObeliskSimpleAssertionASTNodeOp<string mnemonic>
    : ObeliskAssertionASTNodeOp<mnemonic, (ins
    BoolAttr:$is_null,
    BoolAttr:$has_repetition,
    OptionalAttr<ObeliskSVSequenceRepetitionKind>:$repetition_kind,
    OptionalAttr<I64Attr>:$repetition_min,
    OptionalAttr<I64Attr>:$repetition_max,
    BoolAttr:$repetition_is_unbounded
  ), [ObeliskVerifyRepetitionMetadata]>;

class ObeliskSequenceConcatAssertionASTNodeOp<string mnemonic>
    : ObeliskAssertionASTNodeOp<mnemonic, (ins DictArrayAttr:$delays)>;

class ObeliskSequenceWithMatchAssertionASTNodeOp<string mnemonic>
    : ObeliskAssertionASTNodeOp<mnemonic, (ins
    I64Attr:$match_item_count,
    BoolAttr:$has_repetition,
    OptionalAttr<ObeliskSVSequenceRepetitionKind>:$repetition_kind,
    OptionalAttr<I64Attr>:$repetition_min,
    OptionalAttr<I64Attr>:$repetition_max,
    BoolAttr:$repetition_is_unbounded
  ), [ObeliskVerifyRepetitionMetadata]>;

class ObeliskUnaryAssertionASTNodeOp<string mnemonic>
    : ObeliskAssertionASTNodeOp<mnemonic, (ins
    ObeliskSVAssertionUnaryOperator:$operator_kind,
    BoolAttr:$has_range,
    OptionalAttr<I64Attr>:$range_min,
    OptionalAttr<I64Attr>:$range_max,
    BoolAttr:$range_is_unbounded
  ), [ObeliskVerifySequenceRangeMetadata]>;

class ObeliskBinaryAssertionASTNodeOp<string mnemonic>
    : ObeliskAssertionASTNodeOp<mnemonic, (ins
        ObeliskSVAssertionBinaryOperator:$operator_kind,
        OptionalAttr<TypeAttr>:$operator_range
      )>;

class ObeliskMatchItemsAssertionASTNodeOp<string mnemonic>
    : ObeliskAssertionASTNodeOp<mnemonic, (ins I64Attr:$match_item_count)>;

class ObeliskStrongWeakAssertionASTNodeOp<string mnemonic>
    : ObeliskAssertionASTNodeOp<mnemonic,
        (ins ObeliskSVAssertionStrength:$strength)>;

class ObeliskAbortAssertionASTNodeOp<string mnemonic>
    : ObeliskAssertionASTNodeOp<mnemonic, (ins
    ObeliskSVAssertionAbortAction:$action,
    BoolAttr:$is_synchronous
  )>;

class ObeliskConditionalAssertionASTNodeOp<string mnemonic>
    : ObeliskAssertionASTNodeOp<mnemonic, (ins BoolAttr:$has_else)>;

class ObeliskCaseAssertionASTNodeOp<string mnemonic>
    : ObeliskAssertionASTNodeOp<mnemonic, (ins
    ArrayAttr:$item_group_sizes,
    BoolAttr:$has_default
  )>;

class ObeliskPatternASTNodeOp<string mnemonic, dag patternArgs = (ins)>
    : ObeliskASTNodeOp<mnemonic, [], patternArgs>;

class ObeliskReferencePatternASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [], (ins
        SymbolRefAttr:$referenced_symbol,
        StrAttr:$referenced_path,
        OptionalAttr<I64Attr>:$field_ordinal,
        OptionalAttr<I64Attr>:$packed_offset
      )> {
  let hasVerifier = 1;
}

class ObeliskStructurePatternASTNodeOp<string mnemonic>
    : ObeliskPatternASTNodeOp<mnemonic,
        (ins DenseI64ArrayAttr:$field_ordinals)> {
  let hasVerifier = 1;
}

class ObeliskBinsASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [ObeliskSemanticDeclarativeNode]>;
class ObeliskRandSeqASTNodeOp<string mnemonic>
    : ObeliskASTNodeOp<mnemonic, [ObeliskSemanticDeclarativeNode]>;

#endif // OBELISK_DIALECT_OBELISK_OBELISKASTOPSBASE_TD
