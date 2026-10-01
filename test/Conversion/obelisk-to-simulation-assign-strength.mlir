// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.unsupported_assign_strength attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_assign_strength", name = "unsupported_assign_strength", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.unsupported_assign_strength attributes {hierarchical_name = "unsupported_assign_strength", is_uninstantiated = false, name = "unsupported_assign_strength", node_id = 3 : i64, referenced_path = "unsupported_assign_strength", referenced_symbol = @s0.unsupported_assign_strength} {
      obelisk.sv.symbol.instance_body @s4.unsupported_assign_strength attributes {hierarchical_name = "unsupported_assign_strength", name = "unsupported_assign_strength", node_id = 4 : i64} {
        obelisk.sv.symbol.net @s5.value attributes {hierarchical_name = "unsupported_assign_strength.value", is_implicit = false, name = "value", net_kind = 1 : i32, node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.continuous_assign @s6 attributes {delay_fs = array<i64: 2000000, 5000000>, drive_strength0 = 2 : i32, drive_strength1 = 3 : i32, hierarchical_name = "unsupported_assign_strength", node_id = 6 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {node_id = 8 : i64, referenced_path = "unsupported_assign_strength.value", referenced_symbol = @s1.$root::@s3.unsupported_assign_strength::@s4.unsupported_assign_strength::@s5.value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.conversion attributes {node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
            }
          }
          obelisk.sv.timing.delay3 attributes {delay_count = 2 : i64, node_id = 11 : i64} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
            obelisk.sv.expression.integer_literal attributes {constant_value = "5", node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
      }
    }
  }
}

// IEEE 1800-2017 10.3.4: the two polarities retain independent strengths.
// CHECK: simulation.driver.decl {{[0-9]+}} in {{[0-9]+}} drives {{[0-9]+}} : !simulation.logic<1> design
// CHECK-SAME: strength0 = 5 : i32
// CHECK-SAME: strength1 = 3 : i32
// IEEE 1800-2017 10.3.3 applies gate transition rules to a scalar continuous
// assignment. With two values, the turn-off delay is the lesser delay (28.16).
// CHECK: simulation.func private @unit_0
// CHECK-SAME: simulation.propagation_delays = array<i64: 2, 5>
// CHECK-DAG: %[[RISE:.*]] = simulation.time.constant 2
// CHECK-DAG: %[[FALL:.*]] = simulation.time.constant 5
// CHECK: simulation.driver.drive_inertial %{{.*}} = %{{.*}} after[%[[RISE]], %[[FALL]], %[[RISE]]] site {{[0-9]+}} : 0 vector = false
