//===- SlangOpsBase.td - Semantic AST operation schemas -*- tablegen -*-===//

#ifndef OBELISK_DIALECT_SLANG_SLANGOPSBASE_TD
#define OBELISK_DIALECT_SLANG_SLANGOPSBASE_TD

include "obelisk/Dialect/Slang/SlangDialect.td"
include "obelisk/Dialect/Slang/SlangEnums.td"
include "obelisk/Dialect/Slang/SlangTypes.td"
include "mlir/IR/OpBase.td"
include "mlir/IR/SymbolInterfaces.td"

def SlangSemanticASTNode : NativeOpTrait<"SemanticASTNode">;
def SlangSemanticDeclarativeNode
    : NativeOpTrait<"SemanticDeclarativeNode">;
def RejectInvalidSemanticNode : NativeOpTrait<"RejectInvalidSemanticNode">;
def SlangVerifySemanticReferenceGraph
    : NativeOpTrait<"VerifySemanticReferenceGraph">;
def SlangVerifyRepetitionMetadata
    : NativeOpTrait<"VerifyRepetitionMetadata">;
def SlangVerifySequenceRangeMetadata
    : NativeOpTrait<"VerifySequenceRangeMetadata">;

// Only provenance is common to every semantic node. Category and kind data is
// declared by the narrower schemas below so malformed operations are rejected
// by generated ODS verification instead of being accepted as sparse payloads.
class SlangASTNodeOp<string mnemonic, list<Trait> traits = [],
                     dag semanticArgs = (ins)>
    : Op<SlangDialect, mnemonic,
         !listconcat([NoTerminator, SingleBlock, SlangSemanticASTNode],
                     traits)> {
  let summary = "Concrete elaborated slang semantic AST node";
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

class SlangInvalidASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [RejectInvalidSemanticNode]>;

class SlangSymbolASTNodeOp<string mnemonic, list<Trait> traits = [],
                           dag symbolArgs = (ins)>
    : SlangASTNodeOp<mnemonic, !listconcat([Symbol], traits),
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
class SlangSymbolTableASTNodeOp<string mnemonic, dag symbolArgs = (ins),
                                 list<Trait> traits = []>
    : SlangSymbolASTNodeOp<mnemonic,
        !listconcat([SymbolTable, IsolatedFromAbove], traits),
        symbolArgs>;

class SlangRootASTNodeOp<string mnemonic>
    : SlangSymbolASTNodeOp<mnemonic,
        [SymbolTable, IsolatedFromAbove, SlangVerifySemanticReferenceGraph]>;

class SlangConstantSymbolASTNodeOp<string mnemonic>
    : SlangSymbolASTNodeOp<mnemonic, [], (ins StrAttr:$constant_value)>;

class SlangDirectedSymbolASTNodeOp<string mnemonic>
    : SlangSymbolASTNodeOp<mnemonic, [], (ins SlangArgumentDirection:$direction)>;

class SlangDefinitionASTNodeOp<string mnemonic>
    : SlangSymbolTableASTNodeOp<mnemonic,
        (ins SlangDefinitionKind:$definition_kind)>;

class SlangProceduralBlockASTNodeOp<string mnemonic>
    : SlangSymbolASTNodeOp<mnemonic, [],
        (ins SlangProceduralBlockKind:$procedure_kind)>;

class SlangStatementBlockASTNodeOp<string mnemonic>
    : SlangSymbolTableASTNodeOp<mnemonic,
        (ins SlangStatementBlockKind:$block_kind)>;

class SlangSubroutineASTNodeOp<string mnemonic>
    : SlangSymbolTableASTNodeOp<mnemonic, (ins
        SlangSubroutineKind:$subroutine_kind,
        DefaultValuedAttr<SlangVariableLifetime,
          "::obelisk::slangir::VariableLifetime::Automatic">:$default_lifetime,
        DefaultValuedAttr<SlangVisibility,
          "::obelisk::slangir::Visibility::Public">:$member_visibility,
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

class SlangMethodPrototypeASTNodeOp<string mnemonic>
    : SlangSymbolTableASTNodeOp<mnemonic, (ins
        SlangSubroutineKind:$subroutine_kind,
        DefaultValuedAttr<SlangVisibility,
          "::obelisk::slangir::Visibility::Public">:$member_visibility,
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
      ), [SlangSemanticDeclarativeNode]>;

class SlangVariableSymbolASTNodeOp<string mnemonic,
                                   dag variableArgs = (ins)>
    : SlangSymbolASTNodeOp<mnemonic, [], !con((ins
        DefaultValuedAttr<SlangVariableLifetime,
          "::obelisk::slangir::VariableLifetime::Automatic">:$lifetime,
        DefaultValuedAttr<SlangRandMode,
          "::obelisk::slangir::RandMode::None">:$rand_mode,
        OptionalAttr<UnitAttr>:$is_const,
        OptionalAttr<UnitAttr>:$is_compiler_generated,
        OptionalAttr<UnitAttr>:$is_immutable_coverage_option,
        OptionalAttr<UnitAttr>:$is_coverage_sample_formal,
        OptionalAttr<UnitAttr>:$is_checker_free_variable,
        OptionalAttr<UnitAttr>:$is_ref_static
      ), variableArgs)>;

class SlangClassPropertyASTNodeOp<string mnemonic>
    : SlangVariableSymbolASTNodeOp<mnemonic,
        (ins DefaultValuedAttr<SlangVisibility,
          "::obelisk::slangir::Visibility::Public">:$member_visibility)>;

class SlangFormalArgumentASTNodeOp<string mnemonic>
    : SlangVariableSymbolASTNodeOp<mnemonic, (ins
        SlangArgumentDirection:$direction,
        OptionalAttr<SymbolRefAttr>:$merged_variable_symbol,
        OptionalAttr<StrAttr>:$merged_variable_path
      )>;

class SlangIteratorASTNodeOp<string mnemonic>
    : SlangVariableSymbolASTNodeOp<mnemonic, (ins
        TypeAttr:$array_type,
        StrAttr:$index_method_name
      )>;

class SlangClockVarASTNodeOp<string mnemonic>
    : SlangVariableSymbolASTNodeOp<mnemonic, (ins
        SlangArgumentDirection:$direction,
        SlangEdgeKind:$input_edge,
        SlangEdgeKind:$output_edge,
        BoolAttr:$has_input_delay,
        BoolAttr:$has_output_delay
      )>;

class SlangLocalAssertionVarASTNodeOp<string mnemonic>
    : SlangVariableSymbolASTNodeOp<mnemonic, (ins
        OptionalAttr<SymbolRefAttr>:$formal_port_symbol,
        OptionalAttr<StrAttr>:$formal_port_path
      )>;

class SlangFieldASTNodeOp<string mnemonic>
    : SlangVariableSymbolASTNodeOp<mnemonic, (ins
        I64Attr:$bit_offset,
        I64Attr:$field_index
      )>;

class SlangConstraintBlockASTNodeOp<string mnemonic>
    : SlangSymbolTableASTNodeOp<mnemonic, (ins
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
      ), [SlangSemanticDeclarativeNode]>;

class SlangGenericClassDefASTNodeOp<string mnemonic>
    : SlangSymbolTableASTNodeOp<mnemonic, (ins
        BoolAttr:$is_interface,
        I64Attr:$specialization_count,
        OptionalAttr<SymbolRefAttr>:$first_forward_declaration_symbol,
        OptionalAttr<StrAttr>:$first_forward_declaration_path
      ), [SlangSemanticDeclarativeNode]>;

class SlangInstanceASTNodeOp<string mnemonic>
    : SlangSymbolTableASTNodeOp<mnemonic, (ins
        OptionalAttr<SymbolRefAttr>:$referenced_symbol,
        OptionalAttr<StrAttr>:$referenced_path,
        OptionalAttr<BoolAttr>:$is_uninstantiated
      )>;

def PortConnectionOp : Op<SlangDialect, "port.connection", [
    NoTerminator, SlangSemanticASTNode
  ]> {
  let summary = "one resolved scalar elaborated port connection";
  let description = [{
    Records Slang's resolved formal association independently of source child
    order. `internal` contains the instance-side endpoint expression and
    `actual` contains the fully converted parent-side expression. Either
    region can be empty for interface and open associations.
  }];
  let arguments = (ins
    I64Attr:$node_id,
    SymbolRefAttr:$formal_symbol,
    StrAttr:$formal_path,
    I64Attr:$formal_ordinal,
    OptionalAttr<StrAttr>:$formal_name,
    SlangArgumentDirection:$direction,
    TypeAttr:$formal_type,
    BoolAttr:$is_net,
    BoolAttr:$is_ansi,
    BoolAttr:$actual_is_constant,
    SlangPortConnectionKind:$provenance,
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

class SlangContinuousAssignASTNodeOp<string mnemonic>
    : SlangSymbolASTNodeOp<mnemonic, [], (ins
        OptionalAttr<StrAttr>:$unsupported_strength,
        OptionalAttr<StrAttr>:$unsupported_delay
      )>;

class SlangPrimitiveInstanceASTNodeOp<string mnemonic>
    : SlangSymbolASTNodeOp<mnemonic, [], (ins
        OptionalAttr<StrAttr>:$unsupported_strength,
        OptionalAttr<StrAttr>:$unsupported_delay
      )>;

class SlangNetSymbolASTNodeOp<string mnemonic>
    : SlangSymbolASTNodeOp<mnemonic, [], (ins
        SlangNetKind:$net_kind,
        BoolAttr:$is_implicit,
        OptionalAttr<StrAttr>:$unsupported_strength,
        OptionalAttr<StrAttr>:$unsupported_delay
      )>;

// slang data types are Symbols. Aggregate, class, and covergroup types are
// additionally Scopes and therefore use the scope form.
class SlangTypeASTNodeOp<string mnemonic, list<Trait> traits = [],
                         dag typeArgs = (ins)>
    : SlangASTNodeOp<mnemonic, !listconcat([Symbol], traits),
        !con((ins
          StrAttr:$sym_name,
          OptionalAttr<StrAttr>:$name,
          OptionalAttr<StrAttr>:$hierarchical_name,
          TypeAttr:$semantic_type
        ), typeArgs)>;

class SlangScopeTypeASTNodeOp<string mnemonic, dag typeArgs = (ins),
                               list<Trait> traits = []>
    : SlangTypeASTNodeOp<mnemonic,
        !listconcat([SymbolTable, IsolatedFromAbove], traits), typeArgs>;

class SlangNetTypeASTNodeOp<string mnemonic>
    : SlangTypeASTNodeOp<mnemonic, [], (ins
        SlangNetKind:$net_kind,
        TypeAttr:$data_type,
        BoolAttr:$is_builtin,
        OptionalAttr<SymbolRefAttr>:$resolution_function_symbol,
        OptionalAttr<StrAttr>:$resolution_function_path
      )>;

class SlangClassTypeASTNodeOp<string mnemonic>
    : SlangScopeTypeASTNodeOp<mnemonic, (ins
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
      ), [SlangSemanticDeclarativeNode]>;

class SlangCovergroupTypeASTNodeOp<string mnemonic>
    : SlangScopeTypeASTNodeOp<mnemonic,
        (ins OptionalAttr<TypeAttr>:$base_group,
             I64Attr:$constructor_argument_count,
             I64Attr:$sample_formal_count,
             BoolAttr:$has_coverage_event),
        [SlangSemanticDeclarativeNode]>;

class SlangCovergroupBodyASTNodeOp<string mnemonic>
    : SlangSymbolTableASTNodeOp<mnemonic, (ins I64Attr:$option_count)>;

class SlangCoverpointASTNodeOp<string mnemonic>
    : SlangSymbolTableASTNodeOp<mnemonic, (ins
        BoolAttr:$has_iff,
        I64Attr:$option_count
      )>;

class SlangCoverageBinASTNodeOp<string mnemonic>
    : SlangSymbolASTNodeOp<mnemonic, [SlangSemanticDeclarativeNode], (ins
        SlangCoverageBinKind:$bins_kind,
        BoolAttr:$is_array,
        BoolAttr:$is_wildcard,
        BoolAttr:$is_default,
        BoolAttr:$is_default_sequence,
        BoolAttr:$has_iff,
        BoolAttr:$has_number_of_bins,
        BoolAttr:$has_set_coverage,
        BoolAttr:$has_with,
        I64Attr:$value_count,
        I64Attr:$transition_set_count
      )>;

class SlangExpressionASTNodeOp<string mnemonic, dag expressionArgs = (ins)>
    : SlangASTNodeOp<mnemonic, [],
        !con((ins TypeAttr:$semantic_type), expressionArgs)>;

class SlangStructuredAssignmentPatternASTNodeOp<string mnemonic>
    : SlangExpressionASTNodeOp<mnemonic, (ins
        I64Attr:$member_setter_count,
        I64Attr:$type_setter_count,
        I64Attr:$index_setter_count,
        BoolAttr:$has_default_setter
      )> {
  let hasVerifier = 1;
}

class SlangConstantExpressionASTNodeOp<string mnemonic>
    : SlangExpressionASTNodeOp<mnemonic, (ins
        StrAttr:$constant_value,
        OptionalAttr<BoolAttr>:$is_declared_unsized
      )>;

class SlangTimeLiteralASTNodeOp<string mnemonic>
    : SlangExpressionASTNodeOp<mnemonic,
        (ins StrAttr:$constant_value, StrAttr:$time_scale)>;

class SlangReferenceExpressionASTNodeOp<string mnemonic>
    : SlangExpressionASTNodeOp<mnemonic, (ins
        SymbolRefAttr:$referenced_symbol,
        StrAttr:$referenced_path
      )>;

class SlangCallExpressionASTNodeOp<string mnemonic>
    : SlangExpressionASTNodeOp<mnemonic, (ins
        StrAttr:$callee_name,
        BoolAttr:$is_system_call,
        SlangSubroutineKind:$subroutine_kind,
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
        ArrayAttr:$constraint_restrictions,
        OptionalAttr<DenseI64ArrayAttr>:$defaulted_arguments
      )>;

class SlangNewClassExpressionASTNodeOp<string mnemonic>
    : SlangExpressionASTNodeOp<mnemonic, (ins
        BoolAttr:$is_super_class
      )>;

class SlangNewCovergroupExpressionASTNodeOp<string mnemonic>
    : SlangExpressionASTNodeOp<mnemonic, (ins I64Attr:$argument_count)>;

class SlangUnaryExpressionASTNodeOp<string mnemonic>
    : SlangExpressionASTNodeOp<mnemonic,
        (ins SlangUnaryOperator:$operator_kind)>;

class SlangBinaryExpressionASTNodeOp<string mnemonic>
    : SlangExpressionASTNodeOp<mnemonic,
        (ins SlangBinaryOperator:$operator_kind)>;

class SlangConditionalExpressionASTNodeOp<string mnemonic>
    : SlangExpressionASTNodeOp<mnemonic, (ins
        I64Attr:$condition_count,
        DenseI64ArrayAttr:$condition_pattern_flags
      )> {
  let hasVerifier = 1;
}

class SlangAssignmentExpressionASTNodeOp<string mnemonic>
    : SlangExpressionASTNodeOp<mnemonic, (ins
        SlangAssignmentKind:$assignment_kind,
        DefaultValuedAttr<BoolAttr, "false">:$has_timing_control,
        OptionalAttr<SlangBinaryOperator>:$operator_kind
      )>;

class SlangRangeSelectExpressionASTNodeOp<string mnemonic>
    : SlangExpressionASTNodeOp<mnemonic,
        (ins SlangRangeSelectionKind:$selection_kind)>;

class SlangStatementASTNodeOp<string mnemonic> : SlangASTNodeOp<mnemonic>;

class SlangForLoopStatementASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [], (ins
        I64Attr:$initializer_count,
        BoolAttr:$has_condition,
        I64Attr:$step_count
      )> {
  let hasVerifier = 1;
}

class SlangForeachLoopStatementASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [], (ins
        DictArrayAttr:$loop_dimensions
      )> {
  let hasVerifier = 1;
}

class SlangBlockStatementASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [], (ins
        DefaultValuedAttr<SlangStatementBlockKind,
          "::obelisk::slangir::StatementBlockKind::Sequential">:$block_kind,
        OptionalAttr<SymbolRefAttr>:$block_symbol,
        OptionalAttr<StrAttr>:$block_path
      )>;

class SlangDisableStatementASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [], (ins
        OptionalAttr<SymbolRefAttr>:$target_symbol,
        OptionalAttr<StrAttr>:$target_path,
        DefaultValuedAttr<BoolAttr, "false">:$is_hierarchical
      )>;

class SlangVariableDeclarationStatementASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [], (ins
        SymbolRefAttr:$referenced_symbol,
        StrAttr:$referenced_path
      )>;

class SlangWaitOrderStatementASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [], (ins
        DefaultValuedAttr<I64Attr, "0">:$event_count,
        DefaultValuedAttr<BoolAttr, "false">:$has_success_action,
        DefaultValuedAttr<BoolAttr, "false">:$has_failure_action
      )>;

class SlangEventTriggerStatementASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [], (ins
        DefaultValuedAttr<BoolAttr, "false">:$is_nonblocking,
        DefaultValuedAttr<BoolAttr, "false">:$has_timing_control
      )>;

class SlangConditionalStatementASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [], (ins
        SlangUniquePriorityCheck:$check_kind,
        I64Attr:$condition_count,
        DenseI64ArrayAttr:$condition_pattern_flags,
        BoolAttr:$has_else
      )> {
  let hasVerifier = 1;
}

class SlangCaseStatementASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [], (ins
        SlangCaseCondition:$condition_kind,
        SlangUniquePriorityCheck:$check_kind,
        I64Attr:$item_count,
        DenseI64ArrayAttr:$item_label_counts,
        BoolAttr:$has_default
      )> {
  let hasVerifier = 1;
}

class SlangPatternCaseStatementASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [], (ins
        SlangCaseCondition:$condition_kind,
        SlangUniquePriorityCheck:$check_kind,
        I64Attr:$item_count,
        DenseI64ArrayAttr:$item_filter_flags,
        BoolAttr:$has_default
      )> {
  let hasVerifier = 1;
}

class SlangInsideExpressionASTNodeOp<string mnemonic>
    : SlangExpressionASTNodeOp<mnemonic, (ins I64Attr:$item_count)> {
  let hasVerifier = 1;
}

class SlangValueRangeExpressionASTNodeOp<string mnemonic>
    : SlangExpressionASTNodeOp<mnemonic,
        (ins SlangValueRangeKind:$range_kind)> {
  let hasVerifier = 1;
}

class SlangDistExpressionASTNodeOp<string mnemonic>
    : SlangExpressionASTNodeOp<mnemonic, (ins
        I64Attr:$item_count,
        DenseI64ArrayAttr:$item_has_weight,
        DenseI64ArrayAttr:$item_weight_kinds,
        BoolAttr:$has_default_weight,
        I64Attr:$default_weight_kind
      )>;

class SlangImmediateAssertionStatementASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [SlangSemanticDeclarativeNode], (ins
        SlangAssertionKind:$assertion_kind,
        BoolAttr:$is_deferred,
        BoolAttr:$is_final,
        BoolAttr:$has_pass_action,
        BoolAttr:$has_fail_action
      )>;

class SlangConcurrentAssertionStatementASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [SlangSemanticDeclarativeNode], (ins
        SlangAssertionKind:$assertion_kind,
        BoolAttr:$has_pass_action,
        BoolAttr:$has_fail_action
      )>;

class SlangProceduralAssignStatementASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [], (ins BoolAttr:$is_force)>;
class SlangProceduralDeassignStatementASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [], (ins BoolAttr:$is_release)>;
class SlangTimingASTNodeOp<string mnemonic> : SlangASTNodeOp<mnemonic>;

class SlangDelay3TimingASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [], (ins I64Attr:$delay_count)>;

class SlangSignalEventTimingASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [], (ins
        SlangEdgeKind:$edge_kind,
        BoolAttr:$has_iff
      )>;

class SlangEventListTimingASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [], (ins I64Attr:$event_count)>;

class SlangBlockEventListTimingASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [], (ins ArrayAttr:$event_is_begin)>;
class SlangConstraintASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [SlangSemanticDeclarativeNode]>;

class SlangConstraintListASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [SlangSemanticDeclarativeNode], (ins I64Attr:$item_count)>;

class SlangExpressionConstraintASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [SlangSemanticDeclarativeNode], (ins BoolAttr:$is_soft)>;

class SlangConditionalConstraintASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [SlangSemanticDeclarativeNode], (ins BoolAttr:$has_else)>;

class SlangCountedConstraintASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [SlangSemanticDeclarativeNode], (ins I64Attr:$item_count)>;

class SlangSolveBeforeConstraintASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [SlangSemanticDeclarativeNode], (ins
        I64Attr:$solve_count,
        I64Attr:$after_count
      )>;

class SlangForeachConstraintASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [SlangSemanticDeclarativeNode], (ins DictArrayAttr:$loop_dimensions)>;

class SlangAssertionASTNodeOp<string mnemonic, dag assertionArgs = (ins),
                              list<Trait> traits = []>
    : SlangASTNodeOp<mnemonic,
        !listconcat([SlangSemanticDeclarativeNode], traits), !con(
        (ins OptionalAttr<TypeAttr>:$semantic_type), assertionArgs)>;

class SlangSimpleAssertionASTNodeOp<string mnemonic>
    : SlangAssertionASTNodeOp<mnemonic, (ins
    BoolAttr:$is_null,
    BoolAttr:$has_repetition,
    OptionalAttr<SlangSequenceRepetitionKind>:$repetition_kind,
    OptionalAttr<I64Attr>:$repetition_min,
    OptionalAttr<I64Attr>:$repetition_max,
    BoolAttr:$repetition_is_unbounded
  ), [SlangVerifyRepetitionMetadata]>;

class SlangSequenceConcatAssertionASTNodeOp<string mnemonic>
    : SlangAssertionASTNodeOp<mnemonic, (ins DictArrayAttr:$delays)>;

class SlangSequenceWithMatchAssertionASTNodeOp<string mnemonic>
    : SlangAssertionASTNodeOp<mnemonic, (ins
    I64Attr:$match_item_count,
    BoolAttr:$has_repetition,
    OptionalAttr<SlangSequenceRepetitionKind>:$repetition_kind,
    OptionalAttr<I64Attr>:$repetition_min,
    OptionalAttr<I64Attr>:$repetition_max,
    BoolAttr:$repetition_is_unbounded
  ), [SlangVerifyRepetitionMetadata]>;

class SlangUnaryAssertionASTNodeOp<string mnemonic>
    : SlangAssertionASTNodeOp<mnemonic, (ins
    SlangAssertionUnaryOperator:$operator_kind,
    BoolAttr:$has_range,
    OptionalAttr<I64Attr>:$range_min,
    OptionalAttr<I64Attr>:$range_max,
    BoolAttr:$range_is_unbounded
  ), [SlangVerifySequenceRangeMetadata]>;

class SlangBinaryAssertionASTNodeOp<string mnemonic>
    : SlangAssertionASTNodeOp<mnemonic, (ins
        SlangAssertionBinaryOperator:$operator_kind,
        OptionalAttr<TypeAttr>:$operator_range
      )>;

class SlangMatchItemsAssertionASTNodeOp<string mnemonic>
    : SlangAssertionASTNodeOp<mnemonic, (ins I64Attr:$match_item_count)>;

class SlangStrongWeakAssertionASTNodeOp<string mnemonic>
    : SlangAssertionASTNodeOp<mnemonic,
        (ins SlangAssertionStrength:$strength)>;

class SlangAbortAssertionASTNodeOp<string mnemonic>
    : SlangAssertionASTNodeOp<mnemonic, (ins
    SlangAssertionAbortAction:$action,
    BoolAttr:$is_synchronous
  )>;

class SlangConditionalAssertionASTNodeOp<string mnemonic>
    : SlangAssertionASTNodeOp<mnemonic, (ins BoolAttr:$has_else)>;

class SlangCaseAssertionASTNodeOp<string mnemonic>
    : SlangAssertionASTNodeOp<mnemonic, (ins
    ArrayAttr:$item_group_sizes,
    BoolAttr:$has_default
  )>;

class SlangPatternASTNodeOp<string mnemonic, dag patternArgs = (ins)>
    : SlangASTNodeOp<mnemonic, [], patternArgs>;

class SlangReferencePatternASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [], (ins
        SymbolRefAttr:$referenced_symbol,
        StrAttr:$referenced_path,
        OptionalAttr<I64Attr>:$field_ordinal,
        OptionalAttr<I64Attr>:$packed_offset
      )> {
  let hasVerifier = 1;
}

class SlangStructurePatternASTNodeOp<string mnemonic>
    : SlangPatternASTNodeOp<mnemonic,
        (ins DenseI64ArrayAttr:$field_ordinals)> {
  let hasVerifier = 1;
}

class SlangBinsASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [SlangSemanticDeclarativeNode]>;
class SlangRandSeqASTNodeOp<string mnemonic>
    : SlangASTNodeOp<mnemonic, [SlangSemanticDeclarativeNode]>;

#endif // OBELISK_DIALECT_SLANG_SLANGOPSBASE_TD
