// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @m attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @i attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 3 : i64, referenced_path = "m", referenced_symbol = @m} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "m", name = "m", node_id = 4 : i64} {
        obelisk.sv.type.covergroup_type @cg attributes {constructor_argument_count = 0 : i64, constructor_formals = [], coverage_event_kind = 0 : i32, has_coverage_event = false, hierarchical_name = "m.cg", name = "cg", node_id = 8 : i64, sample_formal_count = 0 : i64, sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@body::@cg>} {
          obelisk.sv.symbol.covergroup_body @s9 attributes {hierarchical_name = "m.cg", node_id = 9 : i64, option_count = 0 : i64} {
            obelisk.sv.symbol.coverpoint @cp attributes {expression_roles = [0 : i32], has_iff = false, hierarchical_name = "m.cg.cp", name = "cp", node_id = 33 : i64, option_count = 0 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 999 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
              }
              obelisk.sv.symbol.coverage_bin @dynamic attributes {bins_kind = 0 : i32, child_roles = array<i64: 5>, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "m.cg.cp.dynamic", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "dynamic", node_id = 54 : i64, transition_range_has_repeat_from = array<i64>, transition_range_has_repeat_to = array<i64>, transition_range_item_counts = array<i64>, transition_range_repeat_kinds = array<i64>, transition_set_count = 0 : i64, transition_set_range_counts = array<i64>, value_count = 1 : i64} {
                obelisk.sv.expression.value_range attributes {node_id = 55 : i64, range_kind = 0 : i32, semantic_type = !obelisk.void} {
                  obelisk.sv.expression.conversion attributes {node_id = 56 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 58 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: simulation.covergroup.decl
// CHECK-NOT: obelisk.sv.
