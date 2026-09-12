// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | %python %S/Inputs/dump-coverage-schema.py \
// RUN:   | FileCheck %s

module attributes {obelisk.coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 0 : i64, sym_name = "root"} {
    obelisk.sv.type.covergroup_type attributes {
        constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        hierarchical_name = "cg", name = "cg",
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [],
        semantic_type = !obelisk.covergroup_handle<@root::@cg>,
        sym_name = "cg"} {
      obelisk.sv.symbol.covergroup_body attributes {
          hierarchical_name = "cg", node_id = 2 : i64,
          option_count = 0 : i64, sym_name = "body"} {
        obelisk.sv.symbol.coverpoint attributes {
            expression_roles = [0 : i32], has_iff = false,
            hierarchical_name = "cg.cp", name = "cp",
            node_id = 3 : i64, option_count = 0 : i64,
            semantic_type = !obelisk.integral<3, false, false, 2 : 0, bit>,
            sym_name = "cp"} {
          obelisk.sv.expression.integer_literal attributes {
              constant_value = "0", node_id = 4 : i64,
              semantic_type = !obelisk.integral<3, false, false, 2 : 0, bit>} {
          }
          // IEEE 1800-2023 19.5.4 through 19.5.6 permit wildcard ordinary
          // transition bins to coexist with fixed-length transition
          // exclusions. Preserve both words symbolically in the v1 schema.
          // CHECK-DAG: functional_bin id=[[WILD:[1-9][0-9]*]] item=[[ITEM:[1-9][0-9]*]] name=wild kind=2 flags=16
          // CHECK-DAG: functional_bin id=[[IGNORED:[1-9][0-9]*]] item=[[ITEM]] name=ignored kind=2 flags=4
          // CHECK-DAG: transition_program bin=[[WILD]] item=[[ITEM]] {{.*}} alternative_count=1
          // CHECK-DAG: transition_program bin=[[IGNORED]] item=[[ITEM]] {{.*}} alternative_count=1
          obelisk.sv.symbol.coverage_bin attributes {
              bins_kind = 0 : i32, child_roles = array<i64: 6, 6>,
              has_iff = false, has_number_of_bins = false,
              has_set_coverage = false, has_with = false, is_array = false,
              hierarchical_name = "cg.cp.wild", name = "wild",
              is_default = false, is_default_sequence = false,
              is_wildcard = true, node_id = 5 : i64, sym_name = "wild",
              transition_range_has_repeat_from = array<i64: 0, 0>,
              transition_range_has_repeat_to = array<i64: 0, 0>,
              transition_range_item_counts = array<i64: 1, 1>,
              transition_range_repeat_kinds = array<i64: 0, 0>,
              transition_set_count = 1 : i64,
              transition_set_range_counts = array<i64: 2>,
              value_count = 0 : i64} {
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "3'b0x?", node_id = 6 : i64,
                semantic_type = !obelisk.integral<3, false, true, 2 : 0, logic>} {
            }
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "3'b1z?", node_id = 7 : i64,
                semantic_type = !obelisk.integral<3, false, true, 2 : 0, logic>} {
            }
          }
          obelisk.sv.symbol.coverage_bin attributes {
              bins_kind = 2 : i32, child_roles = array<i64: 6, 6>,
              has_iff = false, has_number_of_bins = false,
              has_set_coverage = false, has_with = false, is_array = false,
              hierarchical_name = "cg.cp.ignored", name = "ignored",
              is_default = false, is_default_sequence = false,
              is_wildcard = false, node_id = 8 : i64, sym_name = "ignored",
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
        }
      }
    }
  }
}
