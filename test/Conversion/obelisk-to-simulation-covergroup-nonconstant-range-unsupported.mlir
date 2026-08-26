// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64, sym_name = "m"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 3 : i64, referenced_path = "m", referenced_symbol = @m, sym_name = "i"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "m", name = "m", node_id = 4 : i64, sym_name = "body"} {
        obelisk.sv.type.covergroup_type attributes {constructor_argument_count = 0 : i64, has_coverage_event = false, hierarchical_name = "m.cg", name = "cg", node_id = 8 : i64, sample_formal_count = 0 : i64, semantic_type = !obelisk.covergroup_handle<@root::@body::@cg>, sym_name = "cg"} {
          obelisk.sv.symbol.covergroup_body attributes {hierarchical_name = "m.cg", node_id = 9 : i64, option_count = 0 : i64, sym_name = "s9"} {
            obelisk.sv.symbol.coverpoint attributes {has_iff = false, hierarchical_name = "m.cg.cp", name = "cp", node_id = 33 : i64, option_count = 0 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, sym_name = "cp"} {
              obelisk.sv.symbol.coverage_bin attributes {bins_kind = 0 : i32, has_iff = false, has_number_of_bins = false, has_set_coverage = false, has_with = false, hierarchical_name = "m.cg.cp.dynamic", is_array = false, is_default = false, is_default_sequence = false, is_wildcard = false, name = "dynamic", node_id = 54 : i64, sym_name = "dynamic", transition_set_count = 0 : i64, value_count = 1 : i64} {
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

// CHECK: coverage bin range bounds must be elaboration-time constants

