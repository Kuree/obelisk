// RUN: not talos-opt %s '--lower-talos-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  talos.sv.symbol.definition @s0.unsupported_assign_delay attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_assign_delay", name = "unsupported_assign_delay", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    talos.sv.symbol.instance @s3.unsupported_assign_delay attributes {hierarchical_name = "unsupported_assign_delay", is_uninstantiated = false, name = "unsupported_assign_delay", node_id = 3 : i64, referenced_path = "unsupported_assign_delay", referenced_symbol = @s0.unsupported_assign_delay} {
      talos.sv.symbol.instance_body @s4.unsupported_assign_delay attributes {hierarchical_name = "unsupported_assign_delay", name = "unsupported_assign_delay", node_id = 4 : i64} {
        talos.sv.symbol.net @s5.value attributes {hierarchical_name = "unsupported_assign_delay.value", is_implicit = false, name = "value", net_kind = 1 : i32, node_id = 5 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
        talos.sv.symbol.continuous_assign @s6 attributes {hierarchical_name = "unsupported_assign_delay", node_id = 6 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, unsupported_delay = "#1 "} {
          talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 7 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
            talos.sv.expression.named_value attributes {node_id = 8 : i64, referenced_path = "unsupported_assign_delay.value", referenced_symbol = @s1.$root::@s3.unsupported_assign_delay::@s4.unsupported_assign_delay::@s5.value, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
            }
            talos.sv.expression.conversion attributes {node_id = 9 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
              talos.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 10 : i64, semantic_type = !talos.ranged_packed_array<0 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
              }
            }
          }
          talos.sv.timing.delay attributes {node_id = 11 : i64} {
            talos.sv.expression.integer_literal attributes {constant_value = "1", node_id = 12 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
      }
    }
  }
}

// CHECK: continuous-assignment delays are not supported: #1
