//===- TalosASTOpsBase.td - High-level SV operation schemas -*- tablegen -*-===//

#ifndef TALOS_DIALECT_TALOS_TALOSASTOPSBASE_TD
#define TALOS_DIALECT_TALOS_TALOSASTOPSBASE_TD

include "talos/Dialect/Talos/TalosDialect.td"
include "talos/Dialect/Talos/TalosEnums.td"
include "talos/Dialect/Talos/TalosTypes.td"
include "mlir/IR/OpBase.td"
include "mlir/IR/SymbolInterfaces.td"

def TalosSemanticASTNode : NativeOpTrait<"SemanticASTNode">;
def TalosSemanticDeclarativeNode
    : NativeOpTrait<"SemanticDeclarativeNode">;
def TalosSemanticExpressionNode
    : NativeOpTrait<"SemanticExpressionNode">;
def TalosSemanticTimingNode : NativeOpTrait<"SemanticTimingNode">;
def TalosSemanticReferenceRootNode
    : NativeOpTrait<"SemanticReferenceRootNode">;
def TalosPatternVariableNode : NativeOpTrait<"PatternVariableNode">;
def TalosTaggedPatternNode : NativeOpTrait<"TaggedPatternNode">;
def TalosPatternBindingNode : NativeOpTrait<"PatternBindingNode">;
def TalosAggregateFieldNode : NativeOpTrait<"AggregateFieldNode">;
def TalosFormalArgumentNode : NativeOpTrait<"FormalArgumentNode">;
def TalosCoverageGroupNode : NativeOpTrait<"CoverageGroupNode">;
def TalosCoveragePointNode : NativeOpTrait<"CoveragePointNode">;
def TalosCoverageCrossNode : NativeOpTrait<"CoverageCrossNode">;
def TalosCoverageBinNode : NativeOpTrait<"CoverageBinNode">;
def TalosCoverageBlockEventNode
    : NativeOpTrait<"CoverageBlockEventNode">;
def TalosCoverageClockingEventNode
    : NativeOpTrait<"CoverageClockingEventNode">;
def TalosCoverageOptionNode : NativeOpTrait<"CoverageOptionNode">;
def TalosCoverageSelectorNode : NativeOpTrait<"CoverageSelectorNode">;
def TalosRejectInvalidSemanticNode
    : NativeOpTrait<"RejectInvalidSemanticNode">;
def TalosVerifySemanticReferenceGraph
    : NativeOpTrait<"VerifySemanticReferenceGraph">;
def TalosVerifyRepetitionMetadata
    : NativeOpTrait<"VerifyRepetitionMetadata">;
def TalosVerifySequenceRangeMetadata
    : NativeOpTrait<"VerifySequenceRangeMetadata">;
def TalosVerifyAssertionDeclarationMetadata
    : NativeOpTrait<"VerifyAssertionDeclarationMetadata">;
def TalosVerifyAssertionInvocationMetadata
    : NativeOpTrait<"VerifyAssertionInvocationMetadata">;
def TalosVerifyCheckerInstanceMetadata
    : NativeOpTrait<"VerifyCheckerInstanceMetadata">;
def TalosVerifyProceduralCheckerMetadata
    : NativeOpTrait<"VerifyProceduralCheckerMetadata">;
def TalosVerifyCovergroupTypeMetadata
    : NativeOpTrait<"VerifyCovergroupTypeMetadata">;
def TalosVerifyCoverageOptionContainerMetadata
    : NativeOpTrait<"VerifyCoverageOptionContainerMetadata">;
def TalosVerifyCoverpointMetadata
    : NativeOpTrait<"VerifyCoverpointMetadata">;
def TalosVerifyCoverCrossMetadata
    : NativeOpTrait<"VerifyCoverCrossMetadata">;
def TalosVerifyCoverageOptionMetadata
    : NativeOpTrait<"VerifyCoverageOptionMetadata">;
def TalosVerifyCoverageBinMetadata
    : NativeOpTrait<"VerifyCoverageBinMetadata">;
def TalosVerifyCoverageBlockEventMetadata
    : NativeOpTrait<"VerifyCoverageBlockEventMetadata">;
def TalosVerifyCoverageConditionSelectorMetadata
    : NativeOpTrait<"VerifyCoverageConditionSelectorMetadata">;
def TalosVerifyCoverageUnarySelectorMetadata
    : NativeOpTrait<"VerifyCoverageUnarySelectorMetadata">;
def TalosVerifyCoverageBinarySelectorMetadata
    : NativeOpTrait<"VerifyCoverageBinarySelectorMetadata">;
def TalosVerifyCoverageSetSelectorMetadata
    : NativeOpTrait<"VerifyCoverageSetSelectorMetadata">;
def TalosVerifyCoverageWithSelectorMetadata
    : NativeOpTrait<"VerifyCoverageWithSelectorMetadata">;
def TalosVerifyCoverageCrossIdSelectorMetadata
    : NativeOpTrait<"VerifyCoverageCrossIdSelectorMetadata">;

class TalosASTNodeOp<string mnemonic, list<Trait> traits = [],
                       dag semanticArgs = (ins)>
    : Op<TalosDialect, mnemonic,
         !listconcat([NoTerminator, SingleBlock, TalosSemanticASTNode],
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

class TalosInvalidASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [TalosRejectInvalidSemanticNode]>;

class TalosSymbolASTNodeOp<string mnemonic, list<Trait> traits = [],
                             dag symbolArgs = (ins)>
    : TalosASTNodeOp<mnemonic, !listconcat([Symbol], traits),
        !con((ins
          SymbolNameAttr:$sym_name,
          OptionalAttr<StrAttr>:$name,
          OptionalAttr<StrAttr>:$hierarchical_name,
          OptionalAttr<TypeAttr>:$semantic_type
        ), symbolArgs)> {
  let assemblyFormat = "$sym_name attr-dict-with-keyword $body";
}

// Every semantic scope is a symbol table and is isolated from above. The AST is
// a pure attribute-and-region tree with no SSA values, so isolation is always
// satisfied; declaring it lets MLIR verify and schedule analysis and
// optimization passes over each scope in parallel. Cross-scope references are
// symbolic (SymbolRefAttr), so they legitimately cross the isolation boundary
// through the symbol mechanism rather than through captured SSA values.
class TalosSymbolTableASTNodeOp<string mnemonic, dag symbolArgs = (ins),
                                  list<Trait> traits = []>
    : TalosSymbolASTNodeOp<mnemonic,
        !listconcat([SymbolTable, IsolatedFromAbove], traits),
        symbolArgs>;

class TalosInstanceArrayASTNodeOp<string mnemonic>
    : TalosSymbolTableASTNodeOp<mnemonic,
        (ins OptionalAttr<DenseI64ArrayAttr>:$array_range)> {
  let description = [{
    The body contains one element per source index in ascending numeric index
    order, independent of the declared direction in `array_range`. Nested
    instance-array nodes repeat this contract for the next dimension.
  }];
}

class TalosGenerateBlockArrayASTNodeOp<string mnemonic>
    : TalosSymbolTableASTNodeOp<mnemonic,
        (ins OptionalAttr<DenseI64ArrayAttr>:$array_indices)>;

class TalosRootASTNodeOp<string mnemonic>
    : TalosSymbolASTNodeOp<mnemonic,
        [SymbolTable, IsolatedFromAbove,
         TalosSemanticReferenceRootNode,
         TalosVerifySemanticReferenceGraph]>;

class TalosConstantSymbolASTNodeOp<string mnemonic>
    : TalosSymbolASTNodeOp<mnemonic, [], (ins StrAttr:$constant_value)>;

class TalosParameterSymbolASTNodeOp<string mnemonic>
    : TalosSymbolASTNodeOp<mnemonic, [], (ins
        StrAttr:$constant_value,
        OptionalAttr<UnitAttr>:$is_local_param,
        OptionalAttr<UnitAttr>:$has_explicit_range)>;

class TalosTypeParameterSymbolASTNodeOp<string mnemonic>
    : TalosSymbolASTNodeOp<mnemonic, [],
        (ins OptionalAttr<UnitAttr>:$is_local_param)>;

class TalosDirectedSymbolASTNodeOp<string mnemonic>
    : TalosSymbolASTNodeOp<mnemonic, [],
        (ins TalosSVArgumentDirection:$direction)>;

class TalosDefinitionASTNodeOp<string mnemonic>
    : TalosSymbolTableASTNodeOp<mnemonic,
        (ins TalosSVDefinitionKind:$definition_kind)>;

class TalosProceduralBlockASTNodeOp<string mnemonic>
    : TalosSymbolASTNodeOp<mnemonic, [],
        (ins TalosSVProceduralBlockKind:$procedure_kind)>;

class TalosStatementBlockASTNodeOp<string mnemonic>
    : TalosSymbolTableASTNodeOp<mnemonic,
        (ins TalosSVStatementBlockKind:$block_kind)>;

class TalosSubroutineASTNodeOp<string mnemonic>
    : TalosSymbolTableASTNodeOp<mnemonic, (ins
        TalosSVSubroutineKind:$subroutine_kind,
        DefaultValuedAttr<TalosSVVariableLifetime,
          "::talos::ir::SVVariableLifetime::Automatic">:$default_lifetime,
        DefaultValuedAttr<TalosSVVisibility,
          "::talos::ir::SVVisibility::Public">:$member_visibility,
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

class TalosMethodPrototypeASTNodeOp<string mnemonic>
    : TalosSymbolTableASTNodeOp<mnemonic, (ins
        TalosSVSubroutineKind:$subroutine_kind,
        DefaultValuedAttr<TalosSVVisibility,
          "::talos::ir::SVVisibility::Public">:$member_visibility,
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
      ), [TalosSemanticDeclarativeNode]>;

class TalosVariableSymbolASTNodeOp<string mnemonic,
                                      dag variableArgs = (ins),
                                      list<Trait> traits = []>
    : TalosSymbolASTNodeOp<mnemonic, traits, !con((ins
        DefaultValuedAttr<TalosSVVariableLifetime,
          "::talos::ir::SVVariableLifetime::Automatic">:$lifetime,
        DefaultValuedAttr<TalosSVRandMode,
          "::talos::ir::SVRandMode::None">:$rand_mode,
        OptionalAttr<UnitAttr>:$is_const,
        OptionalAttr<UnitAttr>:$is_compiler_generated,
        OptionalAttr<UnitAttr>:$is_immutable_coverage_option,
        OptionalAttr<UnitAttr>:$is_coverage_sample_formal,
        OptionalAttr<UnitAttr>:$is_checker_free_variable,
        OptionalAttr<UnitAttr>:$is_ref_static
      ), variableArgs)>;

class TalosClassPropertyASTNodeOp<string mnemonic>
    : TalosVariableSymbolASTNodeOp<mnemonic,
        (ins DefaultValuedAttr<TalosSVVisibility,
          "::talos::ir::SVVisibility::Public">:$member_visibility)>;

class TalosFormalArgumentASTNodeOp<string mnemonic>
    : TalosVariableSymbolASTNodeOp<mnemonic, (ins
        TalosSVArgumentDirection:$direction,
        OptionalAttr<SymbolRefAttr>:$merged_variable_symbol,
        OptionalAttr<StrAttr>:$merged_variable_path
      ), [TalosFormalArgumentNode]>;

class TalosIteratorASTNodeOp<string mnemonic>
    : TalosVariableSymbolASTNodeOp<mnemonic, (ins
        TypeAttr:$array_type,
        StrAttr:$index_method_name
      )>;

class TalosClockVarASTNodeOp<string mnemonic>
    : TalosVariableSymbolASTNodeOp<mnemonic, (ins
        TalosSVArgumentDirection:$direction,
        EdgeKind:$input_edge,
        EdgeKind:$output_edge,
        BoolAttr:$has_input_delay,
        BoolAttr:$has_output_delay
      )>;

class TalosClockingBlockASTNodeOp<string mnemonic>
    : TalosSymbolTableASTNodeOp<mnemonic, (ins
        BoolAttr:$is_default,
        BoolAttr:$is_global
      )>;

class TalosAssertionPortASTNodeOp<string mnemonic>
    : TalosSymbolASTNodeOp<mnemonic, [TalosSemanticDeclarativeNode], (ins
        OptionalAttr<TalosSVArgumentDirection>:$direction,
        BoolAttr:$has_default_value,
        BoolAttr:$is_local_variable
      )>;

class TalosAssertionDeclarationASTNodeOp<string mnemonic>
    : TalosSymbolTableASTNodeOp<mnemonic, (ins
        I64Attr:$port_count,
        SymbolRefArrayAttr:$port_symbols,
        StrArrayAttr:$port_paths,
        BoolAttr:$has_default_instance,
        OptionalAttr<SymbolRefAttr>:$default_clocking_symbol,
        OptionalAttr<StrAttr>:$default_clocking_path
      ), [TalosVerifyAssertionDeclarationMetadata]>;

class TalosLocalAssertionVarASTNodeOp<string mnemonic>
    : TalosVariableSymbolASTNodeOp<mnemonic, (ins
        OptionalAttr<SymbolRefAttr>:$formal_port_symbol,
        OptionalAttr<StrAttr>:$formal_port_path
      )>;

class TalosCheckerDeclarationASTNodeOp<string mnemonic>
    : TalosSymbolTableASTNodeOp<mnemonic, (ins
        I64Attr:$port_count,
        SymbolRefArrayAttr:$port_symbols,
        StrArrayAttr:$port_paths
      ), [TalosVerifyAssertionDeclarationMetadata]>;

class TalosCheckerInstanceASTNodeOp<string mnemonic>
    : TalosSymbolTableASTNodeOp<mnemonic, (ins
        SymbolRefAttr:$referenced_checker_symbol,
        StrAttr:$referenced_checker_path,
        I64Attr:$connection_count,
        SymbolRefArrayAttr:$connection_formal_symbols,
        StrArrayAttr:$connection_formal_paths,
        DenseI64ArrayAttr:$connection_actual_kinds,
        DenseI64ArrayAttr:$connection_has_actual,
        DenseI64ArrayAttr:$connection_has_output_initial,
        DenseI64ArrayAttr:$connection_attribute_counts,
        BoolAttr:$is_procedural
      ), [TalosVerifyCheckerInstanceMetadata]>;

class TalosCheckerInstanceBodyASTNodeOp<string mnemonic>
    : TalosSymbolTableASTNodeOp<mnemonic, (ins
        SymbolRefAttr:$referenced_checker_symbol,
        StrAttr:$referenced_checker_path,
        SymbolRefAttr:$parent_instance_symbol,
        StrAttr:$parent_instance_path,
        I64Attr:$instance_depth,
        I64Attr:$instance_flags,
        BoolAttr:$is_procedural
      )>;

class TalosFieldASTNodeOp<string mnemonic>
    : TalosVariableSymbolASTNodeOp<mnemonic, (ins
        I64Attr:$bit_offset,
        I64Attr:$field_index
      ), [TalosAggregateFieldNode]>;

class TalosPatternVarASTNodeOp<string mnemonic>
    : TalosVariableSymbolASTNodeOp<mnemonic, (ins),
        [TalosPatternBindingNode]>;

class TalosConstraintBlockASTNodeOp<string mnemonic>
    : TalosSymbolTableASTNodeOp<mnemonic, (ins
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
      ), [TalosSemanticDeclarativeNode]>;

class TalosGenericClassDefASTNodeOp<string mnemonic>
    : TalosSymbolTableASTNodeOp<
          mnemonic,
          (ins BoolAttr:$is_interface,
              OptionalAttr<SymbolRefAttr>:$lexical_definition,
              I64Attr:$specialization_count,
              OptionalAttr<SymbolRefAttr>:$first_forward_declaration_symbol,
              OptionalAttr<StrAttr>:$first_forward_declaration_path),
          [TalosSemanticDeclarativeNode,
           DeclareOpInterfaceMethods<SymbolUserOpInterface>]>;

class TalosInstanceASTNodeOp<string mnemonic>
    : TalosSymbolTableASTNodeOp<mnemonic, (ins
        OptionalAttr<SymbolRefAttr>:$referenced_symbol,
        OptionalAttr<StrAttr>:$referenced_path,
        OptionalAttr<BoolAttr>:$is_uninstantiated,
        OptionalAttr<BoolAttr>:$is_virtual_interface_type_instance
      )>;

def SVPortConnectionOp : Op<TalosDialect, "sv.port.connection", [
    NoTerminator, TalosSemanticASTNode
  ]> {
  let summary = "one resolved scalar elaborated port connection";
  let arguments = (ins
    I64Attr:$node_id,
    SymbolRefAttr:$formal_symbol,
    StrAttr:$formal_path,
    I64Attr:$formal_ordinal,
    OptionalAttr<StrAttr>:$formal_name,
    TalosSVArgumentDirection:$direction,
    TypeAttr:$formal_type,
    BoolAttr:$is_net,
    BoolAttr:$is_ansi,
    BoolAttr:$actual_is_constant,
    OptionalAttr<BoolAttr>:$unconnected_drive_value,
    TalosSVPortConnectionKind:$provenance,
    OptionalAttr<SymbolRefAttr>:$internal_symbol,
    OptionalAttr<StrAttr>:$internal_path,
    OptionalAttr<SymbolRefAttr>:$interface_instance_symbol,
    OptionalAttr<StrAttr>:$interface_instance_path,
    OptionalAttr<DenseI64ArrayAttr>:$interface_shape,
    OptionalAttr<StrAttr>:$selected_modport,
    OptionalAttr<TalosSVDriveStrength>:$drive_strength0,
    OptionalAttr<TalosSVDriveStrength>:$drive_strength1,
    OptionalAttr<StrAttr>:$unsupported_delay
  );
  let regions = (region AnyRegion:$internal, AnyRegion:$actual);
  let assemblyFormat = "attr-dict-with-keyword $internal $actual";
}

class TalosContinuousAssignASTNodeOp<string mnemonic>
    : TalosSymbolASTNodeOp<mnemonic, [], (ins
        OptionalAttr<TalosSVDriveStrength>:$drive_strength0,
        OptionalAttr<TalosSVDriveStrength>:$drive_strength1,
        OptionalAttr<DenseI64ArrayAttr>:$delay_fs,
        OptionalAttr<StrAttr>:$unsupported_delay
      )>;

class TalosPrimitiveInstanceASTNodeOp<string mnemonic>
    : TalosSymbolASTNodeOp<mnemonic, [], (ins
        OptionalAttr<TalosSVDriveStrength>:$drive_strength0,
        OptionalAttr<TalosSVDriveStrength>:$drive_strength1,
        OptionalAttr<DenseI64ArrayAttr>:$delay_fs,
        OptionalAttr<StrAttr>:$unsupported_delay
      )>;

class TalosNetSymbolASTNodeOp<string mnemonic>
    : TalosSymbolASTNodeOp<mnemonic, [], (ins
        TalosSVNetKind:$net_kind,
        BoolAttr:$is_implicit,
        DefaultValuedAttr<TalosSVNetExpansionHint,
          "::talos::ir::SVNetExpansionHint::None">:$expansion_hint,
        OptionalAttr<SymbolRefAttr>:$nettype_symbol,
        OptionalAttr<StrAttr>:$nettype_path,
        OptionalAttr<SymbolRefAttr>:$resolution_function_symbol,
        OptionalAttr<StrAttr>:$resolution_function_path,
        OptionalAttr<TalosSVDriveStrength>:$drive_strength0,
        OptionalAttr<TalosSVDriveStrength>:$drive_strength1,
        OptionalAttr<TalosSVChargeStrength>:$charge_strength,
        OptionalAttr<DenseI64ArrayAttr>:$delay_fs,
        OptionalAttr<StrAttr>:$unsupported_delay
      )>;

class TalosTypeASTNodeOp<string mnemonic, list<Trait> traits = [],
                           dag typeArgs = (ins)>
    : TalosASTNodeOp<mnemonic, !listconcat([Symbol], traits),
        !con((ins
          SymbolNameAttr:$sym_name,
          OptionalAttr<StrAttr>:$name,
          OptionalAttr<StrAttr>:$hierarchical_name,
          TypeAttr:$semantic_type
        ), typeArgs)> {
  let assemblyFormat = "$sym_name attr-dict-with-keyword $body";
}

class TalosScopeTypeASTNodeOp<string mnemonic, dag typeArgs = (ins),
                               list<Trait> traits = []>
    : TalosTypeASTNodeOp<mnemonic,
        !listconcat([SymbolTable, IsolatedFromAbove], traits), typeArgs>;

class TalosNetTypeASTNodeOp<string mnemonic>
    : TalosTypeASTNodeOp<mnemonic, [], (ins
        TalosSVNetKind:$net_kind,
        TypeAttr:$data_type,
        BoolAttr:$is_builtin,
        OptionalAttr<SymbolRefAttr>:$aliased_nettype_symbol,
        OptionalAttr<StrAttr>:$aliased_nettype_path,
        OptionalAttr<SymbolRefAttr>:$resolution_function_symbol,
        OptionalAttr<StrAttr>:$resolution_function_path
      )>;

class TalosClassTypeASTNodeOp<string mnemonic>
    : TalosScopeTypeASTNodeOp<
          mnemonic,
          (ins BoolAttr:$is_abstract,
              OptionalAttr<SymbolRefAttr>:$lexical_definition,
              BoolAttr:$is_interface, BoolAttr:$is_final,
              BoolAttr:$is_uninstantiated, OptionalAttr<TypeAttr>:$base_class,
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
              BoolAttr:$has_base_constructor_call, I64Attr:$bitstream_width,
              BoolAttr:$has_cycles),
          [TalosSemanticDeclarativeNode,
           DeclareOpInterfaceMethods<SymbolUserOpInterface>]>;

class TalosCovergroupTypeASTNodeOp<string mnemonic>
    : TalosScopeTypeASTNodeOp<mnemonic,
        (ins OptionalAttr<TypeAttr>:$base_group,
             I64Attr:$constructor_argument_count,
             SymbolRefArrayAttr:$constructor_formals,
             I64Attr:$sample_formal_count,
             SymbolRefArrayAttr:$sample_formals,
             TalosSVCoverageEventKind:$coverage_event_kind,
             BoolAttr:$has_coverage_event),
        [TalosSemanticDeclarativeNode, TalosCoverageGroupNode,
         TalosVerifyCovergroupTypeMetadata]>;

class TalosCovergroupBodyASTNodeOp<string mnemonic>
    : TalosSymbolTableASTNodeOp<mnemonic, (ins I64Attr:$option_count),
        [TalosVerifyCoverageOptionContainerMetadata]>;

class TalosCoverpointASTNodeOp<string mnemonic>
    : TalosSymbolTableASTNodeOp<mnemonic, (ins
        BoolAttr:$has_iff,
        DefaultValuedAttr<BoolAttr, "false">:$has_explicit_type,
        I64Attr:$option_count,
        TalosSVCoverageExpressionRoleArrayAttr:$expression_roles
      ), [TalosCoveragePointNode, TalosVerifyCoverpointMetadata]>;

class TalosCoverCrossASTNodeOp<string mnemonic>
    : TalosSymbolTableASTNodeOp<mnemonic, (ins
        I64Attr:$target_count,
        SymbolRefArrayAttr:$target_symbols,
        BoolAttr:$has_iff,
        I64Attr:$option_count,
        TalosSVCoverageExpressionRoleArrayAttr:$expression_roles
      ), [TalosCoverageCrossNode, TalosVerifyCoverCrossMetadata]>;

def SVCoverageOptionOp : TalosASTNodeOp<"sv.coverage.option",
    [TalosSemanticDeclarativeNode, TalosCoverageOptionNode,
     TalosVerifyCoverageOptionMetadata], (ins
      TalosSVCoverageOptionOwnerKind:$owner_kind,
      TalosSVCoverageOptionScopeKind:$scope_kind,
      TalosSVCoverageOptionKind:$option_kind,
      SymbolRefAttr:$owner_symbol
    )>;

class TalosCoverageBinASTNodeOp<string mnemonic>
    : TalosSymbolASTNodeOp<mnemonic,
        [TalosSemanticDeclarativeNode, TalosCoverageBinNode,
         TalosVerifyCoverageBinMetadata], (ins
        TalosSVCoverageBinKind:$bins_kind,
        BoolAttr:$is_array,
        BoolAttr:$is_wildcard,
        BoolAttr:$is_default,
        BoolAttr:$is_default_sequence,
        BoolAttr:$has_iff,
        BoolAttr:$has_number_of_bins,
        BoolAttr:$has_set_coverage,
        BoolAttr:$has_with,
        I64Attr:$value_count,
        I64Attr:$transition_set_count,
        DenseI64ArrayAttr:$child_roles,
        DenseI64ArrayAttr:$transition_set_range_counts,
        DenseI64ArrayAttr:$transition_range_item_counts,
        DenseI64ArrayAttr:$transition_range_repeat_kinds,
        DenseI64ArrayAttr:$transition_range_has_repeat_from,
        DenseI64ArrayAttr:$transition_range_has_repeat_to
      )>;

class TalosExpressionASTNodeOp<string mnemonic, dag expressionArgs = (ins),
                                 list<Trait> traits = []>
    : TalosASTNodeOp<mnemonic,
        !listconcat([TalosSemanticExpressionNode], traits),
        !con((ins TypeAttr:$semantic_type), expressionArgs)>;

class TalosAssertionInstanceExpressionASTNodeOp<string mnemonic>
    : TalosExpressionASTNodeOp<mnemonic, (ins
        SymbolRefAttr:$referenced_symbol,
        StrAttr:$referenced_path,
        I64Attr:$argument_count,
        SymbolRefArrayAttr:$argument_formal_symbols,
        StrArrayAttr:$argument_formal_paths,
        DenseI64ArrayAttr:$argument_kinds,
        I64Attr:$local_variable_count,
        SymbolRefArrayAttr:$local_variable_symbols,
        StrArrayAttr:$local_variable_paths,
        DenseI64ArrayAttr:$local_variable_has_initializer,
        BoolAttr:$is_recursive_property,
        BoolAttr:$has_expanded_body
      ), [TalosVerifyAssertionInvocationMetadata]>;

class TalosStructuredAssignmentPatternASTNodeOp<string mnemonic>
    : TalosExpressionASTNodeOp<mnemonic, (ins
        I64Attr:$member_setter_count,
        OptionalAttr<DenseI64ArrayAttr>:$member_setter_ordinals,
        I64Attr:$type_setter_count,
        OptionalAttr<TypeArrayAttr>:$type_setter_types,
        I64Attr:$index_setter_count,
        BoolAttr:$has_default_setter
      )> {
  let hasVerifier = 1;
}

class TalosStreamingConcatenationASTNodeOp<string mnemonic>
    : TalosExpressionASTNodeOp<mnemonic, (ins
        I64Attr:$slice_size,
        I64Attr:$bitstream_width,
        I64Attr:$stream_count,
        DenseI64ArrayAttr:$stream_with_flags,
        BoolAttr:$is_fixed_size
      )> {
  let hasVerifier = 1;
}

class TalosConstantExpressionASTNodeOp<string mnemonic>
    : TalosExpressionASTNodeOp<mnemonic, (ins
        StrAttr:$constant_value,
        OptionalAttr<BoolAttr>:$is_declared_unsized
      )>;

class TalosTimeLiteralASTNodeOp<string mnemonic>
    : TalosExpressionASTNodeOp<mnemonic,
        (ins StrAttr:$constant_value, StrAttr:$time_scale)>;

class TalosReferenceExpressionASTNodeOp<string mnemonic>
    : TalosExpressionASTNodeOp<mnemonic, (ins
        SymbolRefAttr:$referenced_symbol,
        StrAttr:$referenced_path
      )>;

class TalosCallExpressionASTNodeOp<string mnemonic>
    : TalosExpressionASTNodeOp<mnemonic, (ins
        StrAttr:$callee_name,
        BoolAttr:$is_system_call,
        // Exact bound builtin-method identity, independent of receiver layout.
        UnitAttr:$is_builtin_method,
        TalosSVSubroutineKind:$subroutine_kind,
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
        OptionalAttr<DenseI64ArrayAttr>:$defaulted_arguments,
        OptionalAttr<TalosSVDynamicCastKind>:$dynamic_cast_kind,
        OptionalAttr<StrArrayAttr>:$dynamic_cast_enum_values,
        OptionalAttr<StrArrayAttr>:$enum_method_values,
        OptionalAttr<StrArrayAttr>:$enum_method_names,
        OptionalAttr<StrAttr>:$typename_spelling,
        OptionalAttr<StrArrayAttr>:$readmem_enum_key_values,
        OptionalAttr<StrArrayAttr>:$readmem_enum_element_values,
        // IEEE 1800-2017 13.4.1: casting a call to the void type uses the
        // function as a statement and discards its return value. The call
        // keeps its function semantics there, which 6.24.2 makes observable
        // for $cast: the function form reports a failed cast through its 0
        // result instead of raising the task form's run-time error.
        UnitAttr:$is_void_casted
      ), [PredOpTrait<"builtin methods are system calls",
            CPred<"!getIsBuiltinMethod() || getIsSystemCall()">>]>;

class TalosNewClassExpressionASTNodeOp<string mnemonic>
    : TalosExpressionASTNodeOp<mnemonic, (ins
        BoolAttr:$is_super_class
      )>;

class TalosNewCovergroupExpressionASTNodeOp<string mnemonic>
    : TalosExpressionASTNodeOp<mnemonic, (ins
        I64Attr:$argument_count,
        OptionalAttr<DenseI64ArrayAttr>:$defaulted_arguments
      )>;

class TalosUnaryExpressionASTNodeOp<string mnemonic>
    : TalosExpressionASTNodeOp<mnemonic,
        (ins TalosSVUnaryOperator:$operator_kind)>;

class TalosBinaryExpressionASTNodeOp<string mnemonic>
    : TalosExpressionASTNodeOp<mnemonic,
        (ins TalosSVBinaryOperator:$operator_kind)>;

class TalosConditionalExpressionASTNodeOp<string mnemonic>
    : TalosExpressionASTNodeOp<mnemonic, (ins
        I64Attr:$condition_count,
        DenseI64ArrayAttr:$condition_pattern_flags
      )> {
  let hasVerifier = 1;
}

class TalosAssignmentExpressionASTNodeOp<string mnemonic>
    : TalosExpressionASTNodeOp<mnemonic, (ins
        TalosSVAssignmentKind:$assignment_kind,
        DefaultValuedAttr<BoolAttr, "false">:$has_timing_control,
        OptionalAttr<TalosSVBinaryOperator>:$operator_kind
      )>;

class TalosRangeSelectExpressionASTNodeOp<string mnemonic>
    : TalosExpressionASTNodeOp<mnemonic,
        (ins TalosSVRangeSelectionKind:$selection_kind)>;

class TalosStatementASTNodeOp<string mnemonic> : TalosASTNodeOp<mnemonic>;

class TalosForLoopStatementASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [], (ins
        I64Attr:$initializer_count,
        BoolAttr:$has_condition,
        I64Attr:$step_count,
        DefaultValuedAttr<BoolAttr, "false">:$has_local_variables
      )> {
  let hasVerifier = 1;
}

class TalosForeachLoopStatementASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [], (ins
        DictArrayAttr:$loop_dimensions
      )> {
  let hasVerifier = 1;
}

class TalosBlockStatementASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [], (ins
        DefaultValuedAttr<TalosSVStatementBlockKind,
          "::talos::ir::SVStatementBlockKind::Sequential">:$block_kind,
        OptionalAttr<SymbolRefAttr>:$block_symbol,
        OptionalAttr<StrAttr>:$block_path,
        OptionalAttr<UnitAttr>:$is_implicit_loop_scope
      )> {
  let hasVerifier = 1;
}

class TalosDisableStatementASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [], (ins
        OptionalAttr<SymbolRefAttr>:$target_symbol,
        OptionalAttr<StrAttr>:$target_path,
        DefaultValuedAttr<BoolAttr, "false">:$is_hierarchical
      )>;

class TalosVariableDeclarationStatementASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [], (ins
        SymbolRefAttr:$referenced_symbol,
        StrAttr:$referenced_path
      )>;

class TalosWaitOrderStatementASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [], (ins
        DefaultValuedAttr<I64Attr, "0">:$event_count,
        DefaultValuedAttr<BoolAttr, "false">:$has_success_action,
        DefaultValuedAttr<BoolAttr, "false">:$has_failure_action
      )>;

class TalosEventTriggerStatementASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [], (ins
        DefaultValuedAttr<BoolAttr, "false">:$is_nonblocking,
        DefaultValuedAttr<BoolAttr, "false">:$has_timing_control
      )>;

class TalosConditionalStatementASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [], (ins
        TalosSVUniquePriorityCheck:$check_kind,
        I64Attr:$condition_count,
        DenseI64ArrayAttr:$condition_pattern_flags,
        BoolAttr:$has_else
      )> {
  let hasVerifier = 1;
}

class TalosCaseStatementASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [], (ins
        TalosSVCaseCondition:$condition_kind,
        TalosSVUniquePriorityCheck:$check_kind,
        I64Attr:$item_count,
        DenseI64ArrayAttr:$item_label_counts,
        BoolAttr:$has_default
      )> {
  let hasVerifier = 1;
}

class TalosPatternCaseStatementASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [], (ins
        TalosSVCaseCondition:$condition_kind,
        TalosSVUniquePriorityCheck:$check_kind,
        I64Attr:$item_count,
        DenseI64ArrayAttr:$item_filter_flags,
        BoolAttr:$has_default
      )> {
  let hasVerifier = 1;
}

class TalosRandCaseStatementASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [], (ins I64Attr:$item_count)> {
  let hasVerifier = 1;
}

class TalosInsideExpressionASTNodeOp<string mnemonic>
    : TalosExpressionASTNodeOp<mnemonic, (ins I64Attr:$item_count)> {
  let hasVerifier = 1;
}

class TalosValueRangeExpressionASTNodeOp<string mnemonic>
    : TalosExpressionASTNodeOp<mnemonic,
        (ins TalosSVValueRangeKind:$range_kind)> {
  let hasVerifier = 1;
}

class TalosDistExpressionASTNodeOp<string mnemonic>
    : TalosExpressionASTNodeOp<mnemonic, (ins
        I64Attr:$item_count,
        DenseI64ArrayAttr:$item_has_weight,
        DenseI64ArrayAttr:$item_weight_kinds,
        BoolAttr:$has_default_weight,
        I64Attr:$default_weight_kind
      )>;

class TalosImmediateAssertionStatementASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [TalosSemanticDeclarativeNode], (ins
        TalosSVAssertionKind:$assertion_kind,
        BoolAttr:$is_deferred,
        BoolAttr:$is_final,
        BoolAttr:$has_pass_action,
        BoolAttr:$has_fail_action
      )>;

class TalosConcurrentAssertionStatementASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [TalosSemanticDeclarativeNode], (ins
        TalosSVAssertionKind:$assertion_kind,
        BoolAttr:$has_pass_action,
        BoolAttr:$has_fail_action,
        OptionalAttr<BoolAttr>:$is_procedural,
        OptionalAttr<BoolAttr>:$has_contextual_clock,
        OptionalAttr<BoolAttr>:$starts_on_current_clock,
        OptionalAttr<BoolAttr>:$is_static_time_zero_equivalent,
        OptionalAttr<SymbolRefAttr>:$default_clocking_symbol,
        OptionalAttr<StrAttr>:$default_clocking_path,
        BoolAttr:$has_default_disable
      )>;

class TalosProceduralCheckerStatementASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic,
      [TalosSemanticDeclarativeNode,
       TalosVerifyProceduralCheckerMetadata], (ins
        I64Attr:$instance_count,
        SymbolRefArrayAttr:$instance_symbols,
        StrArrayAttr:$instance_paths
      )>;

class TalosProceduralAssignStatementASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [], (ins BoolAttr:$is_force)>;
class TalosProceduralDeassignStatementASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [], (ins BoolAttr:$is_release)>;
class TalosTimingASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [TalosSemanticTimingNode]>;

class TalosDelay3TimingASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [TalosSemanticTimingNode],
        (ins I64Attr:$delay_count)>;

class TalosSignalEventTimingASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic,
        [TalosSemanticTimingNode, TalosCoverageClockingEventNode], (ins
        EdgeKind:$edge_kind,
        BoolAttr:$has_iff
      )>;

class TalosEventListTimingASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic,
        [TalosSemanticTimingNode, TalosCoverageClockingEventNode],
        (ins I64Attr:$event_count)>;

class TalosBlockEventListTimingASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic,
        [TalosSemanticTimingNode, TalosCoverageBlockEventNode,
         TalosVerifyCoverageBlockEventMetadata],
        (ins TalosSVCoverageBlockEventKindArrayAttr:$event_kinds)>;

class TalosCoverageSelectASTNodeOp<string mnemonic, dag selectArgs = (ins),
                                     list<Trait> traits = []>
    : TalosASTNodeOp<mnemonic,
        !listconcat([TalosSemanticDeclarativeNode,
                     TalosCoverageSelectorNode], traits),
        !con((ins SymbolRefAttr:$enclosing_cross_symbol), selectArgs)>;

class TalosCoverageSelectConditionASTNodeOp<string mnemonic>
    : TalosCoverageSelectASTNodeOp<mnemonic, (ins
        SymbolRefAttr:$target_symbol,
        I64Attr:$intersect_count
      ), [TalosVerifyCoverageConditionSelectorMetadata]>;

class TalosCoverageSelectUnaryASTNodeOp<string mnemonic>
    : TalosCoverageSelectASTNodeOp<mnemonic,
        (ins TalosSVCoverageSelectUnaryOperator:$operator_kind),
        [TalosVerifyCoverageUnarySelectorMetadata]>;

class TalosCoverageSelectBinaryASTNodeOp<string mnemonic>
    : TalosCoverageSelectASTNodeOp<mnemonic,
        (ins TalosSVCoverageSelectBinaryOperator:$operator_kind),
        [TalosVerifyCoverageBinarySelectorMetadata]>;

class TalosCoverageSelectSetExprASTNodeOp<string mnemonic>
    : TalosCoverageSelectASTNodeOp<mnemonic, (ins BoolAttr:$has_matches),
        [TalosVerifyCoverageSetSelectorMetadata]>;

class TalosCoverageSelectWithASTNodeOp<string mnemonic>
    : TalosCoverageSelectASTNodeOp<mnemonic, (ins BoolAttr:$has_matches),
        [TalosVerifyCoverageWithSelectorMetadata]>;

class TalosCoverageSelectCrossIdASTNodeOp<string mnemonic>
    : TalosCoverageSelectASTNodeOp<mnemonic, (ins),
        [TalosVerifyCoverageCrossIdSelectorMetadata]>;
class TalosConstraintASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [TalosSemanticDeclarativeNode]>;

class TalosConstraintListASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [TalosSemanticDeclarativeNode], (ins I64Attr:$item_count)>;

class TalosExpressionConstraintASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [TalosSemanticDeclarativeNode], (ins BoolAttr:$is_soft)>;

class TalosConditionalConstraintASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [TalosSemanticDeclarativeNode], (ins BoolAttr:$has_else)>;

class TalosCountedConstraintASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [TalosSemanticDeclarativeNode], (ins I64Attr:$item_count)>;

class TalosSolveBeforeConstraintASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [TalosSemanticDeclarativeNode], (ins
        I64Attr:$solve_count,
        I64Attr:$after_count
      )>;

class TalosForeachConstraintASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [TalosSemanticDeclarativeNode], (ins DictArrayAttr:$loop_dimensions)>;

class TalosAssertionASTNodeOp<string mnemonic, dag assertionArgs = (ins),
                                list<Trait> traits = []>
    : TalosASTNodeOp<mnemonic,
        !listconcat([TalosSemanticDeclarativeNode], traits), !con(
        (ins OptionalAttr<TypeAttr>:$semantic_type), assertionArgs)>;

class TalosSimpleAssertionASTNodeOp<string mnemonic>
    : TalosAssertionASTNodeOp<mnemonic, (ins
    BoolAttr:$is_null,
    BoolAttr:$has_repetition,
    OptionalAttr<TalosSVSequenceRepetitionKind>:$repetition_kind,
    OptionalAttr<I64Attr>:$repetition_min,
    OptionalAttr<I64Attr>:$repetition_max,
    BoolAttr:$repetition_is_unbounded
  ), [TalosVerifyRepetitionMetadata]>;

class TalosSequenceConcatAssertionASTNodeOp<string mnemonic>
    : TalosAssertionASTNodeOp<mnemonic, (ins DictArrayAttr:$delays)>;

class TalosSequenceWithMatchAssertionASTNodeOp<string mnemonic>
    : TalosAssertionASTNodeOp<mnemonic, (ins
    I64Attr:$match_item_count,
    BoolAttr:$has_repetition,
    OptionalAttr<TalosSVSequenceRepetitionKind>:$repetition_kind,
    OptionalAttr<I64Attr>:$repetition_min,
    OptionalAttr<I64Attr>:$repetition_max,
    BoolAttr:$repetition_is_unbounded
  ), [TalosVerifyRepetitionMetadata]>;

class TalosUnaryAssertionASTNodeOp<string mnemonic>
    : TalosAssertionASTNodeOp<mnemonic, (ins
    TalosSVAssertionUnaryOperator:$operator_kind,
    BoolAttr:$has_range,
    OptionalAttr<I64Attr>:$range_min,
    OptionalAttr<I64Attr>:$range_max,
    BoolAttr:$range_is_unbounded
  ), [TalosVerifySequenceRangeMetadata]>;

class TalosBinaryAssertionASTNodeOp<string mnemonic>
    : TalosAssertionASTNodeOp<mnemonic, (ins
        TalosSVAssertionBinaryOperator:$operator_kind,
        OptionalAttr<TypeAttr>:$operator_range
      )>;

class TalosMatchItemsAssertionASTNodeOp<string mnemonic>
    : TalosAssertionASTNodeOp<mnemonic, (ins I64Attr:$match_item_count)>;

class TalosStrongWeakAssertionASTNodeOp<string mnemonic>
    : TalosAssertionASTNodeOp<mnemonic,
        (ins TalosSVAssertionStrength:$strength)>;

class TalosAbortAssertionASTNodeOp<string mnemonic>
    : TalosAssertionASTNodeOp<mnemonic, (ins
    TalosSVAssertionAbortAction:$action,
    BoolAttr:$is_synchronous
  )>;

class TalosConditionalAssertionASTNodeOp<string mnemonic>
    : TalosAssertionASTNodeOp<mnemonic, (ins BoolAttr:$has_else)>;

class TalosCaseAssertionASTNodeOp<string mnemonic>
    : TalosAssertionASTNodeOp<mnemonic, (ins
    ArrayAttr:$item_group_sizes,
    BoolAttr:$has_default
  )>;

class TalosPatternASTNodeOp<string mnemonic, dag patternArgs = (ins)>
    : TalosASTNodeOp<mnemonic, [], patternArgs>;

class TalosReferencePatternASTNodeOp<string mnemonic,
                                        list<Trait> traits = []>
    : TalosASTNodeOp<mnemonic, traits, (ins
        SymbolRefAttr:$referenced_symbol,
        StrAttr:$referenced_path,
        OptionalAttr<I64Attr>:$field_ordinal,
        OptionalAttr<I64Attr>:$packed_offset
      )> {
  let hasVerifier = 1;
}

class TalosVariablePatternASTNodeOp<string mnemonic>
    : TalosReferencePatternASTNodeOp<mnemonic,
        [TalosPatternVariableNode]>;

class TalosTaggedPatternASTNodeOp<string mnemonic>
    : TalosReferencePatternASTNodeOp<mnemonic,
        [TalosTaggedPatternNode]>;

class TalosStructurePatternASTNodeOp<string mnemonic>
    : TalosPatternASTNodeOp<mnemonic,
        (ins DenseI64ArrayAttr:$field_ordinals)> {
  let hasVerifier = 1;
}

class TalosBinsASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [TalosSemanticDeclarativeNode]>;
class TalosRandSeqASTNodeOp<string mnemonic>
    : TalosASTNodeOp<mnemonic, [TalosSemanticDeclarativeNode]>;

// Preparation materializes one of these non-symbol nodes beneath a
// randsequence statement for every production in that statement's grammar.
// Production declarations are MLIR symbols and must stay in the frozen
// semantic symbol table; this executable snapshot can instead be cloned into
// an isolated simulation function without violating symbol-table nesting.
def SVFrozenRandSeqProductionOp
    : TalosASTNodeOp<"sv.rand_seq.frozen_production", [], (ins
        SymbolRefAttr:$referenced_symbol,
        StrAttr:$referenced_path,
        I64Attr:$argument_count,
        I64Attr:$rule_count,
        DenseI64ArrayAttr:$rule_item_counts,
        DenseI64ArrayAttr:$rule_has_weights,
        DenseI64ArrayAttr:$rule_has_weight_code_blocks,
        DenseI64ArrayAttr:$rule_is_rand_join,
        DenseI64ArrayAttr:$rule_has_rand_join_expressions,
        ArrayAttr:$formal_arguments,
        ArrayAttr:$rule_variables
      )>;

// Source declarations exist independently of any selected generic class
// specialization. Their default subtrees preserve original syntax semantics;
// they never carry an elaborated replacement for an unresolved parameter.
// A generic class's own lexical method identity does not depend on any
// specialization or require binding its signature or body.
// A nested declaration in an unspecialized class is a scope identity, not
// a class type. Its own source inventory never binds dependent signatures.
def SVSourceClassDefinitionSymbolOp
    : TalosSymbolTableASTNodeOp<
          "sv.source.class_definition",
          (ins BoolAttr:$is_abstract, BoolAttr:$is_interface,
              BoolAttr:$is_final, BoolAttr:$is_parameterized)> {
  let summary = "lexical nested class declaration without a specialization";
  let hasVerifier = 1;
}

def SVSourceClassPropertySymbolOp
    : TalosSymbolASTNodeOp<"sv.source.class_property", [], (ins
        TalosSVVariableLifetime:$lifetime,
        TalosSVVisibility:$member_visibility,
        TalosSVRandMode:$rand_mode,
        OptionalAttr<UnitAttr>:$is_const)> {
  let summary = "lexical generic class property without executable storage";
  let description = [{
    Owns exactly one source declared-type node. The declaration preserves its
    lexical identity and modifiers without binding a specialization or initializer.
  }];
  let hasVerifier = 1;
}

def SVSourceClassTypeAliasSymbolOp
    : TalosSymbolASTNodeOp<"sv.source.class_type_alias", [], (ins
        TalosSVVisibility:$member_visibility)> {
  let summary = "lexical generic class typedef without an effective target";
  let hasVerifier = 1;
}

def SVSourceDeclaredTypeOp
    : TalosASTNodeOp<"sv.source.declared_type", [],
        (ins TypeAttr:$semantic_type)> {
  let summary = "exact parameter-independent source declared type";
  let hasVerifier = 1;
}

def SVSourceDeclaredTypeReferenceOp
    : TalosASTNodeOp<"sv.source.declared_type_reference",
        [DeclareOpInterfaceMethods<SymbolUserOpInterface>],
        (ins FlatSymbolRefAttr:$declaration)> {
  let summary = "source declared type naming an exact preceding lexical type";
  let hasVerifier = 1;
}

def SVSourceUnavailableTypeOp
    : TalosASTNodeOp<"sv.source.unavailable_type", [],
        (ins StrAttr:$syntax)> {
  let summary = "source declared type awaiting structural import";
  let hasVerifier = 1;
}

def SVSourceClassExtendsOp
    : TalosASTNodeOp<"sv.source.class_extends",
                       [DeclareOpInterfaceMethods<SymbolUserOpInterface>],
                       (ins OptionalAttr<SymbolRefAttr>:$base_definition,
                           OptionalAttr<StrAttr>:$unavailable_base,
                           OptionalAttr<StrAttr>:$parameter_arguments,
                           OptionalAttr<StrAttr>:$constructor_arguments)> {
  let summary = "lexical class inheritance without choosing a specialization";
  let description = [{
    The base declaration is an exact source class symbol. An unresolved
    dependent base instead retains its source spelling in unavailable_base;
    exactly one representation is required. Explicit parameter and constructor
    argument syntax is retained separately until its source expression graph
    can be represented. An absent list is distinct from an unavailable one.
    The node has no effective type or elaborated body.
  }];
  let hasVerifier = 1;
}

def SVSourceParameterActualOp
    : TalosASTNodeOp<"sv.source.parameter_actual",
                     [DeclareOpInterfaceMethods<SymbolUserOpInterface>],
                     (ins SymbolRefAttr:$parameter, I64Attr:$ordinal,
                         OptionalAttr<UnitAttr>:$is_named,
                         OptionalAttr<UnitAttr>:$use_default,
                         OptionalAttr<StrAttr>:$unavailable_rhs)> {
  let summary = "an exact explicit scalar-instance parameter connection";
  let description = [{
    The source node belongs to the instantiating instance body. Its parameter
    names an exact nonlocal declaration in a directly instantiated child body.
    A supported original value/type is its single child. Unsupported syntax
    and named empty connections are explicit, without substituting an effective
    parameter value/type. Ordinals distinguish source assignment occurrences.
  }];
  let hasVerifier = 1;
}

def SVSourceClassParameterActualOp
    : TalosASTNodeOp<"sv.source.class_parameter_actual",
                       [DeclareOpInterfaceMethods<SymbolUserOpInterface>],
                       (ins SymbolRefAttr:$parameter,
                           OptionalAttr<UnitAttr>:$is_named,
                           OptionalAttr<UnitAttr>:$use_default,
                           OptionalAttr<StrAttr>:$unavailable_rhs)> {
  let summary = "an exact lexical class parameter connection";
  let description = [{
    The parameter is the exact nonlocal declaration of the referenced base.
    A supported explicit value or type is its single child. A named empty
    connection carries use_default and no child, preserving the original
    declaration default. An unsupported expression retains unavailable_rhs
    instead, without an effective value or type.
  }];
  let hasVerifier = 1;
}

def SVSourceClassConstraintSymbolOp
    : TalosSymbolASTNodeOp<"sv.source.class_constraint", [], (ins
        OptionalAttr<UnitAttr>:$is_static,
        OptionalAttr<UnitAttr>:$is_pure,
        OptionalAttr<UnitAttr>:$is_prototype,
        BoolAttr:$has_external_definition)> {
  let summary = "lexical normal constraint without a bound constraint body";
  let description = [{
    Owned by a lexical class definition, with an empty body and no
    semantic_type. The source position is the declaration or prototype
    position. has_external_definition records an exact out-of-block definition
    lookup without binding its constraint expressions.
  }];
  let hasVerifier = 1;
}

def SVSourceClassMethodSymbolOp
    : TalosSymbolASTNodeOp<"sv.source.class_method", [], (ins
        TalosSVSubroutineKind:$subroutine_kind,
        TalosSVVariableLifetime:$default_lifetime,
        TalosSVVisibility:$member_visibility,
        OptionalAttr<UnitAttr>:$is_static,
        OptionalAttr<UnitAttr>:$is_declared_virtual,
        OptionalAttr<UnitAttr>:$is_pure,
        OptionalAttr<UnitAttr>:$is_constructor,
        OptionalAttr<UnitAttr>:$is_extern,
        OptionalAttr<BoolAttr>:$source_virtuality)> {
  let summary = "lexical generic-class method without an effective signature";
  let description = [{
    Owned by a lexical class definition, with an empty body and no
    semantic_type. The symbol preserves declaration identity and source-only
    modifiers; it does not designate a specialized executable method.
  }];
  let hasVerifier = 1;
}

def SVSourceTypeParameterSymbolOp
    : TalosSymbolASTNodeOp<"sv.source.type_parameter", [],
        (ins OptionalAttr<UnitAttr>:$is_local_param)> {
  let summary = "unresolved lexical type parameter of a generic class";
  let description = [{
    The direct parent is a lexical class definition. semantic_type is absent;
    the symbol itself is the unresolved typespec (IEEE 1800-2023 37.25 detail
    11). Its body contains zero or one original type default. An empty body
    means there was no default, while an unsupported-default child preserves
    the distinct case where a source default has no structural import yet.
  }];
  let hasVerifier = 1;
}

def SVSourceValueParameterSymbolOp
    : TalosSymbolASTNodeOp<"sv.source.value_parameter", [],
        (ins OptionalAttr<UnitAttr>:$is_local_param)> {
  let summary = "lexical value parameter without an effective specialization";
  let description = [{
    Directly owned by a generic class definition. Its body contains at most
    one original default node; no semantic_type or elaborated value is bound.
  }];
  let hasVerifier = 1;
}

def SVSourceValueDefaultOp
    : TalosASTNodeOp<"sv.source.value_default"> {
  let summary = "original value-parameter default expression";
  let description = [{
    Owns exactly one typed source expression, independently of the effective
    parameter initializer and its overrides. Integer literal nodes retain their
    self-determined semantic types; references and operators remain unbound.
  }];
  let hasVerifier = 1;
}

def SVSourceParameterReferenceOp
    : TalosASTNodeOp<"sv.source.parameter_reference",
        [DeclareOpInterfaceMethods<SymbolUserOpInterface>],
        (ins FlatSymbolRefAttr:$parameter)> {
  let summary = "exact preceding value parameter in an original default";
  let hasVerifier = 1;
}

def SVSourceUnaryExpressionOp
    : TalosASTNodeOp<"sv.source.unary_expression", [],
        (ins TalosSVUnaryOperator:$operator_kind)> {
  let summary = "unbound pure unary operation in an original default";
  let hasVerifier = 1;
}

def SVSourceBinaryExpressionOp
    : TalosASTNodeOp<"sv.source.binary_expression", [],
        (ins TalosSVBinaryOperator:$operator_kind)> {
  let summary = "unbound binary operation in an original default";
  let hasVerifier = 1;
}

def SVSourceConditionalExpressionOp
    : TalosASTNodeOp<"sv.source.conditional_expression"> {
  let summary = "unbound conditional operation in an original default";
  let description = [{
    Owns the condition, true expression, and false expression in source order.
    None of the branches is selected or evaluated during source import.
  }];
  let hasVerifier = 1;
}

def SVSourceTypeDefaultOp
    : TalosASTNodeOp<"sv.source.type_default", [],
        (ins TypeAttr:$semantic_type)> {
  let summary = "parameter-independent original type default";
  let description = [{
    An exact concrete source type, without evaluating a class specialization.
    The existing vpi_typedef_layers metadata may retain alias spelling. This
    node has an empty body and is owned by a source type parameter.
  }];
  let hasVerifier = 1;
}

def SVSourceTypeReferenceDefaultOp
    : TalosASTNodeOp<"sv.source.type_reference_default",
        [DeclareOpInterfaceMethods<SymbolUserOpInterface>],
        (ins FlatSymbolRefAttr:$parameter)> {
  let summary = "original default naming a preceding lexical type parameter";
  let description = [{
    The symbol reference names the exact preceding source type parameter in
    the same generic class. This node does not bind or resolve that parameter.
    It has an empty body and is owned by another source type parameter.
  }];
  let hasVerifier = 1;
}

def SVSourceUnsupportedDefaultOp
    : TalosASTNodeOp<"sv.source.unsupported_default", [],
        (ins StrAttr:$syntax, StrAttr:$reason)> {
  let summary = "present original default awaiting structural import";
  let description = [{
    Preserves a present default separately from an absent default without
    fabricating its value or type. Only unsupported nodes retain source text;
    supported defaults use typed operations. The body is empty.
  }];
  let hasVerifier = 1;
}

#endif // TALOS_DIALECT_TALOS_TALOSASTOPSBASE_TD
