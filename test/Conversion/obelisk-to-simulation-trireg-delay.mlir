// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.trireg_delay attributes {definition_kind = 0 : i32, hierarchical_name = "trireg_delay", name = "trireg_delay", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.trireg_delay attributes {hierarchical_name = "trireg_delay", is_uninstantiated = false, name = "trireg_delay", node_id = 3 : i64, referenced_path = "trireg_delay", referenced_symbol = @s0.trireg_delay} {
      obelisk.sv.symbol.instance_body @s4.trireg_delay attributes {hierarchical_name = "trireg_delay", name = "trireg_delay", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.net @s5.one attributes {charge_strength = 0 : i32, delay_fs = array<i64: 7000000>, hierarchical_name = "trireg_delay.one", is_implicit = false, name = "one", net_kind = 9 : i32, node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.net @s6.two attributes {charge_strength = 1 : i32, delay_fs = array<i64: 7000000, 11000000>, hierarchical_name = "trireg_delay.two", is_implicit = false, name = "two", net_kind = 9 : i32, node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.net @s7.three attributes {charge_strength = 2 : i32, delay_fs = array<i64: 7000000, 11000000, 13000000>, hierarchical_name = "trireg_delay.three", is_implicit = false, name = "three", net_kind = 9 : i32, node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.net @s8.initialized attributes {delay_fs = array<i64: 2000000, 5000000, 13000000>, hierarchical_name = "trireg_delay.initialized", is_implicit = false, name = "initialized", net_kind = 9 : i32, node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
          }
        }
      }
    }
  }
}

// IEEE 1800-2017 28.16.2: one and two delay values provide driven-state
// rise/fall propagation without charge decay; the third value is decay.
// CHECK-DAG: simulation.net.decl {{[0-9]+}} {{.*}} hierarchy "trireg_delay.one" {{.*}}charge_strength = 1 : i32{{.*}}propagation_delays = array<i64: 7, 7, -1>{{.*}}resolution_kind = 9 : i32
// CHECK-DAG: simulation.net.decl {{[0-9]+}} {{.*}} hierarchy "trireg_delay.two" {{.*}}charge_strength = 2 : i32{{.*}}propagation_delays = array<i64: 7, 11, -1>{{.*}}resolution_kind = 9 : i32
// CHECK-DAG: simulation.net.decl {{[0-9]+}} {{.*}} hierarchy "trireg_delay.three" {{.*}}charge_strength = 4 : i32{{.*}}propagation_delays = array<i64: 7, 11, 13>{{.*}}resolution_kind = 9 : i32

// IEEE 1800-2017 10.3.3: with a declaration assignment these values belong
// to that continuous assignment and are not a net delay or charge decay.
// CHECK: simulation.net.decl {{[0-9]+}} {{.*}} hierarchy "trireg_delay.initialized"
// CHECK-SAME: charge_strength = 2 : i32
// CHECK-SAME: resolution_kind = 9 : i32
// CHECK-NOT: propagation_delays
// CHECK: simulation.code_unit.decl {{[0-9]+}} {{.*}} hierarchy "trireg_delay.initialized.$net_initializer"
// CHECK-LABEL: simulation.func private @unit_0
// CHECK-SAME: simulation.propagation_delays = array<i64: 2, 5, 13>
