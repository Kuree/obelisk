// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' | FileCheck %s --check-prefix=O3

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_delay", name = "unsupported_delay", node_id = 0 : i64, sym_name = "s0.unsupported_delay"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unsupported_delay", is_uninstantiated = false, name = "unsupported_delay", node_id = 3 : i64, referenced_path = "unsupported_delay", referenced_symbol = @s0.unsupported_delay, sym_name = "s3.unsupported_delay"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unsupported_delay", name = "unsupported_delay", node_id = 4 : i64, sym_name = "s4.unsupported_delay", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.net attributes {delay_fs = array<i64: 1000000>, hierarchical_name = "unsupported_delay.value", is_implicit = false, name = "value", net_kind = 1 : i32, node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.value"} {
          obelisk.sv.timing.delay attributes {node_id = 8 : i64} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
        obelisk.sv.symbol.continuous_assign attributes {hierarchical_name = "unsupported_delay", node_id = 10 : i64, sym_name = "s6", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "unsupported_delay.value", referenced_symbol = @s1.$root::@s3.unsupported_delay::@s4.unsupported_delay::@s5.value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.conversion attributes {node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
            }
          }
        }
      }
    }
  }
}

// IEEE 1800-2017 10.3.3: a delay on an uninitialized net declaration delays
// the resolved net update, rather than the individual continuous driver.
// CHECK: obelisk_sim.net.decl [[NET:[0-9]+]] in {{[0-9]+}} : !obelisk_sim.logic<1> design
// CHECK-SAME: propagation_delays = array<i64: 1, 1, 1>
// CHECK: obelisk_sim.driver.decl [[DRIVER:[0-9]+]] in {{[0-9]+}} drives [[NET]]
// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK-SAME: obelisk_sim.delayed_net
// CHECK: obelisk_sim.driver.drive_delayed_net
// CHECK-NOT: obelisk_sim.driver.drive %

// The scheduler side effect prevents compute fusion from replacing the
// delayed publication with an immediate drive at optimization level 3.
// O3: obelisk_sim.net.decl
// O3-SAME: propagation_delays = array<i64: 1, 1, 1>
// O3-LABEL: obelisk_sim.func private @unit_0
// O3: obelisk_sim.driver.drive_delayed_net
// O3-NOT: obelisk_sim.driver.drive %
