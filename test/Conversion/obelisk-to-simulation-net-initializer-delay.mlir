// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' | FileCheck %s --check-prefix=O3

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "initializer_delay", name = "initializer_delay", node_id = 0 : i64, sym_name = "s0.initializer_delay"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "initializer_delay", is_uninstantiated = false, name = "initializer_delay", node_id = 3 : i64, referenced_path = "initializer_delay", referenced_symbol = @s0.initializer_delay, sym_name = "s3.initializer_delay"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "initializer_delay", name = "initializer_delay", node_id = 4 : i64, sym_name = "s4.initializer_delay", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.net attributes {delay_fs = array<i64: 2000000, 5000000>, hierarchical_name = "initializer_delay.value", is_implicit = false, name = "value", net_kind = 1 : i32, node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.value"} {
          obelisk.sv.expression.conversion attributes {node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
            }
          }
          obelisk.sv.timing.delay3 attributes {delay_count = 2 : i64, node_id = 8 : i64} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
            obelisk.sv.expression.integer_literal attributes {constant_value = "5", node_id = 10 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "initializer_delay.observed", lifetime = 1 : i32, name = "observed", node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.observed"} {
          obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "initializer_delay.value", referenced_symbol = @s1.$root::@s3.initializer_delay::@s4.initializer_delay::@s5.value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
          }
        }
      }
    }
  }
}

// A declaration with an initializer is a continuous assignment, so its delay
// belongs to that assignment rather than to the net (IEEE 1800-2017 10.3.3).
// CHECK: obelisk_sim.code_unit.decl {{[0-9]+}} in 1 continuous hierarchy "initializer_delay.value.$net_initializer"
// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK-SAME: obelisk_sim.propagation_delays = array<i64: 2, 5>
// CHECK-DAG: %[[RISE:.*]] = obelisk_sim.time.constant 2
// CHECK-DAG: %[[FALL:.*]] = obelisk_sim.time.constant 5
// CHECK: obelisk_sim.driver.drive_inertial {{.*}} after[%[[RISE]], %[[FALL]], %[[RISE]]] site {{[0-9]+}} : 0 vector = false
// CHECK-NOT: obelisk.sv.timing
// The net is still Z while static initialization runs; the delayed literal
// driver must not participate in the time-zero constant-net fold.
// CHECK-LABEL: obelisk_sim.func private @unit_1
// CHECK: obelisk_sim.net.read

// O3-LABEL: obelisk_sim.func private @unit_0
// O3-COUNT-1: obelisk_sim.driver.drive_inertial
// O3-NOT: obelisk_sim.driver.drive %
