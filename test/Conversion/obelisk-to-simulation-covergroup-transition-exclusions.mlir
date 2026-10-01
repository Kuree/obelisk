// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' > %t.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py < %t.mlir | FileCheck %s

module attributes {obelisk.coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.root @root attributes {
      hierarchical_name = "\\$root ", name = "$root", node_id = 0 : i64
  } {
    obelisk.sv.type.covergroup_type @cg attributes {
        constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        hierarchical_name = "cg", name = "cg", node_id = 1 : i64,
        sample_formal_count = 0 : i64,
        sample_formals = [],
        semantic_type = !obelisk.covergroup_handle<@root::@cg>
    } {
      obelisk.sv.symbol.covergroup_body @body attributes {
          hierarchical_name = "cg", node_id = 2 : i64,
          option_count = 0 : i64} {
        obelisk.sv.symbol.coverpoint @cp attributes {
            expression_roles = [0 : i32], has_iff = false,
            hierarchical_name = "cg.cp", name = "cp", node_id = 3 : i64,
            option_count = 0 : i64,
            semantic_type = !obelisk.integral<3, false, false, 2 : 0, bit>
        } {
          obelisk.sv.expression.integer_literal attributes {
              constant_value = "0", node_id = 4 : i64,
              semantic_type = !obelisk.integral<3, false, false, 2 : 0, bit>} {
          }
          obelisk.sv.symbol.coverage_bin @ordinary attributes {
              bins_kind = 0 : i32, child_roles = array<i64: 6, 6>,
              has_iff = false, has_number_of_bins = false,
              has_set_coverage = false, has_with = false, is_array = false,
              is_default = false, is_default_sequence = false,
              hierarchical_name = "cg.cp.ordinary", is_wildcard = false,
              name = "ordinary", node_id = 5 : i64,
              transition_range_has_repeat_from = array<i64: 0, 0>,
              transition_range_has_repeat_to = array<i64: 0, 0>,
              transition_range_item_counts = array<i64: 1, 1>,
              transition_range_repeat_kinds = array<i64: 0, 0>,
              transition_set_count = 1 : i64,
              transition_set_range_counts = array<i64: 2>,
              value_count = 0 : i64} {
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "1", node_id = 6 : i64,
                semantic_type = !obelisk.integral<3, false, false, 2 : 0, bit>} {
            }
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "2", node_id = 7 : i64,
                semantic_type = !obelisk.integral<3, false, false, 2 : 0, bit>} {
            }
          }
          obelisk.sv.symbol.coverage_bin @ignored attributes {
              bins_kind = 2 : i32, child_roles = array<i64: 6, 6>,
              has_iff = false, has_number_of_bins = false,
              has_set_coverage = false, has_with = false, is_array = false,
              is_default = false, is_default_sequence = false,
              hierarchical_name = "cg.cp.ignored", is_wildcard = false,
              name = "ignored", node_id = 8 : i64,
              transition_range_has_repeat_from = array<i64: 0, 0>,
              transition_range_has_repeat_to = array<i64: 0, 0>,
              transition_range_item_counts = array<i64: 1, 1>,
              transition_range_repeat_kinds = array<i64: 0, 0>,
              transition_set_count = 1 : i64,
              transition_set_range_counts = array<i64: 2>,
              value_count = 0 : i64} {
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "2", node_id = 9 : i64,
                semantic_type = !obelisk.integral<3, false, false, 2 : 0, bit>} {
            }
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "3", node_id = 10 : i64,
                semantic_type = !obelisk.integral<3, false, false, 2 : 0, bit>} {
            }
          }
          obelisk.sv.symbol.coverage_bin @illegal attributes {
              bins_kind = 1 : i32, child_roles = array<i64: 6, 6>,
              has_iff = false, has_number_of_bins = false,
              has_set_coverage = false, has_with = false, is_array = true,
              is_default = false, is_default_sequence = false,
              hierarchical_name = "cg.cp.illegal", is_wildcard = false,
              name = "illegal", node_id = 11 : i64,
              transition_range_has_repeat_from = array<i64: 0, 0>,
              transition_range_has_repeat_to = array<i64: 0, 0>,
              transition_range_item_counts = array<i64: 1, 1>,
              transition_range_repeat_kinds = array<i64: 0, 0>,
              transition_set_count = 1 : i64,
              transition_set_range_counts = array<i64: 2>,
              value_count = 0 : i64} {
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "4", node_id = 12 : i64,
                semantic_type = !obelisk.integral<3, false, false, 2 : 0, bit>} {
            }
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "5", node_id = 13 : i64,
                semantic_type = !obelisk.integral<3, false, false, 2 : 0, bit>} {
            }
          }
        }
      }
    }
  }
}

// CHECK-DAG: functional_bin id=[[ORD:[1-9][0-9]*]] item=[[ITEM:[1-9][0-9]*]] name=ordinary kind=2 flags=0
// CHECK-DAG: functional_bin id=[[IGNORE:[1-9][0-9]*]] item=[[ITEM]] name=ignored kind=2 flags=4
// CHECK-DAG: functional_bin id=[[ILLEGAL:[1-9][0-9]*]] item=[[ITEM]] name=illegal kind=2 flags=8
// CHECK-DAG: transition_program bin=[[ORD]] item=[[ITEM]] first_alternative={{[0-9]+}} alternative_count=1 flags=0
// CHECK-DAG: transition_program bin=[[IGNORE]] item=[[ITEM]] first_alternative={{[0-9]+}} alternative_count=1 flags=0
// CHECK-DAG: transition_program bin=[[ILLEGAL]] item=[[ITEM]] first_alternative={{[0-9]+}} alternative_count=1 flags=0
// CHECK-DAG: functional_bin_plan bin=[[IGNORE]] value_set=0 iff_expression=0 cardinality_expression=0 array_cardinality=0 array_mode=1 distribution=1 flags=0
// CHECK-DAG: functional_bin_plan bin=[[ILLEGAL]] value_set=0 iff_expression=0 cardinality_expression=0 array_cardinality=0 array_mode=2 distribution=2 flags=0
