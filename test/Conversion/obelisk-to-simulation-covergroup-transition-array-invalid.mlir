// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s

module attributes {obelisk.coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.root attributes {node_id = 0 : i64, sym_name = "root"} {
    obelisk.sv.type.covergroup_type attributes {
        constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [],
        semantic_type = !obelisk.covergroup_handle<@root::@cg>,
        sym_name = "cg"} {
      obelisk.sv.symbol.covergroup_body attributes {
          node_id = 2 : i64, option_count = 0 : i64, sym_name = "body"} {
        obelisk.sv.symbol.coverpoint attributes {
            expression_roles = [0 : i32], has_iff = false,
            node_id = 3 : i64, option_count = 0 : i64,
            semantic_type = !obelisk.integral<3, false, false, 2 : 0, bit>,
            sym_name = "cp"} {
          obelisk.sv.expression.integer_literal attributes {
              constant_value = "0", node_id = 4 : i64,
              semantic_type = !obelisk.integral<3, false, false, 2 : 0, bit>} {
          }
          // CHECK-COUNT-2: multiple transition bins cannot contain goto or nonconsecutive repetition because these produce unbounded or varying-length sequences
          // CHECK-NOT: this transition coverage bin is not supported
          obelisk.sv.symbol.coverage_bin attributes {
              bins_kind = 0 : i32, child_roles = array<i64: 6, 6, 7>,
              has_iff = false, has_number_of_bins = false,
              has_set_coverage = false, has_with = false, is_array = true,
              is_default = false, is_default_sequence = false,
              is_wildcard = false, node_id = 5 : i64, sym_name = "bad",
              transition_range_has_repeat_from = array<i64: 0, 1>,
              transition_range_has_repeat_to = array<i64: 0, 0>,
              transition_range_item_counts = array<i64: 1, 1>,
              transition_range_repeat_kinds = array<i64: 0, 3>,
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
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "2", node_id = 8 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
          obelisk.sv.symbol.coverage_bin attributes {
              bins_kind = 0 : i32, child_roles = array<i64: 6, 6, 7>,
              has_iff = false, has_number_of_bins = false,
              has_set_coverage = false, has_with = false, is_array = true,
              is_default = false, is_default_sequence = false,
              is_wildcard = false, node_id = 9 : i64,
              sym_name = "bad_nonconsecutive",
              transition_range_has_repeat_from = array<i64: 0, 1>,
              transition_range_has_repeat_to = array<i64: 0, 0>,
              transition_range_item_counts = array<i64: 1, 1>,
              transition_range_repeat_kinds = array<i64: 0, 2>,
              transition_set_count = 1 : i64,
              transition_set_range_counts = array<i64: 2>,
              value_count = 0 : i64} {
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "1", node_id = 10 : i64,
                semantic_type = !obelisk.integral<3, false, false, 2 : 0, bit>} {
            }
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "2", node_id = 11 : i64,
                semantic_type = !obelisk.integral<3, false, false, 2 : 0, bit>} {
            }
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "2", node_id = 12 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
      }
    }
  }
}
