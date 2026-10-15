//===- CoverageDialect.td - Coverage metadata --------------*- tablegen -*-===//

#ifndef OBELISK_DIALECT_COVERAGE_COVERAGEDIALECT_TD
#define OBELISK_DIALECT_COVERAGE_COVERAGEDIALECT_TD

include "mlir/IR/DialectBase.td"

def CoverageDialect : Dialect {
  let name = "coverage";
  // `obelisk::coverage` is the coverage database library.
  let cppNamespace = "::obelisk::cov";
  let summary = "code and functional coverage metadata (IEEE 1800-2023 19)";
  let description = [{
    `coverage` names the facts coverage elaboration attaches to the design:
    the requested metrics and configuration on the module, line and toggle
    point inventories on declarations, and the identities of covergroup
    expressions, items and formals that lowering threads into the runtime
    coverage schema. It has no operations; its attributes annotate
    operations of other dialects.
  }];
  let useDefaultAttributePrinterParser = 1;
  let usePropertiesForAttributes = 0;
  let hasOperationAttrVerify = 1;
  let discardableAttrs = (ins
    // Module: driver options and source inventory.
    "::mlir::ArrayAttr":$metrics,
    "::mlir::StringAttr":$config,
    "::mlir::ArrayAttr":$prefix_maps,
    "::mlir::ArrayAttr":$source_files,
    // IEEE 1800 revision for revision-sensitive coverage computation,
    // persisted with every functional type.
    "::mlir::IntegerAttr":$language_version,
    // Line counters and flattened toggle bits the embedded schema expects.
    "::mlir::IntegerAttr":$line_point_count,
    "::mlir::IntegerAttr":$toggle_bit_count,
    // Assertion success counters the embedded schema expects.
    "::mlir::IntegerAttr":$assertion_point_count,

    // Transient dense line counter index. Stable entity identity stays in
    // the schema and never depends on this physical index.
    "::mlir::IntegerAttr":$line_point_index,
    // Transient dense assertion counter index of an assertion statement.
    "::mlir::IntegerAttr":$assertion_point_index,
    // Canonical state whose committed transitions are coverage-observable.
    "::mlir::UnitAttr":$toggle_observable,
    // Toggle coverage of a source-visible declaration: its
    // `#coverage.toggle_binding` ranges. Aliases may bind several ranges to
    // one canonical state range.
    "::mlir::ArrayAttr":$toggle_bindings,
    // Module: the flattened toggle shadow, value and unknown planes, at the
    // language-defined initial state.
    "::mlir::DenseI8ArrayAttr":$toggle_initial_value,
    "::mlir::DenseI8ArrayAttr":$toggle_initial_unknown,

    // Stable functional expression identity assigned while the schema and
    // helper contract are built together. Unit lowering copies it to
    // covergroup.sample and never synthesizes an execution-local ordinal.
    "::mlir::IntegerAttr":$functional_expression_id,
    "::mlir::IntegerAttr":$functional_expression_ordinal,
    "::mlir::IntegerAttr":$functional_expression_kind,
    "::mlir::IntegerAttr":$functional_expression_bit_width,
    "::mlir::IntegerAttr":$functional_expression_signedness,
    "::mlir::IntegerAttr":$functional_expression_phase,
    "::mlir::IntegerAttr":$functional_expression_role,
    "::mlir::IntegerAttr":$functional_formal_id,
    "::mlir::IntegerAttr":$functional_item_id,
    // Symbol uses that keep coverage-instrumented code units, callees of
    // constructor and sample expressions, and their class method descriptors
    // alive through devirtualization and symbol DCE. Not merge identities.
    "::mlir::ArrayAttr":$retained_symbols,

    // Covergroup structure: inheritance, option targets and sources.
    "::mlir::TypeAttr":$base_group,
    "::mlir::IntegerAttr":$inherited_origin_type,
    "::mlir::UnitAttr":$inherited_overridden,
    "::mlir::StringAttr":$effective_target_symbol,
    "::mlir::ArrayAttr":$effective_target_symbols,
    // A design-lifetime declaration that directly represents a source object;
    // compiler support state never becomes a coverage obligation.
    "::mlir::UnitAttr":$source_authored,
    // Source type kept when normalization erases reportable identity, such
    // as an enum name.
    "::mlir::TypeAttr":$source_type,
    "::mlir::StringAttr":$type_option_constant,
    "::mlir::TypeAttr":$field_storage_type,
    // Block-event sampling (`@@(begin|end method)`).
    "::mlir::IntegerAttr":$block_event_target_id,
    "::mlir::UnitAttr":$block_event_instance_method,

    // Constructor-time bin `with` predicates are evaluated once per candidate
    // occurrence; cross selectors yield one queue of satisfying tuples, and
    // the target paths name the iterator bindings in the predicate loop.
    "::mlir::DenseI64ArrayAttr":$with_expression_ids,
    "::mlir::DenseI64ArrayAttr":$with_expression_ordinals,
    "::mlir::ArrayAttr":$with_candidate_values,
    "::mlir::StringAttr":$with_iterator_path,
    "::mlir::BoolAttr":$cross_with_scalar,
    "::mlir::TypeAttr":$cross_with_tuple_type,
    "::mlir::IntegerAttr":$cross_with_selector_id,
    "::mlir::ArrayAttr":$cross_with_target_paths
  );
}

#endif // OBELISK_DIALECT_COVERAGE_COVERAGEDIALECT_TD
