// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32, hierarchical_name = "bus_if", name = "bus_if", node_id = 0 : i64, sym_name = "s0.bus_if"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64, sym_name = "s1.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "s2.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, is_virtual_interface_type_instance = false, name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @s1.top, sym_name = "s4.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, sym_name = "s5.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.sentinel", lifetime = 1 : i32, name = "sentinel", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s6.sentinel"} {
        }
        // Slang creates these compile-time-only instances to resolve the two
        // parameterized virtual-interface types. They are not design scopes.
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.bus_if", is_uninstantiated = false, is_virtual_interface_type_instance = true, name = "bus_if", node_id = 7 : i64, referenced_path = "bus_if", referenced_symbol = @s0.bus_if, sym_name = "s7.bus_if"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.bus_if", name = "bus_if", node_id = 8 : i64, sym_name = "s8.bus_if", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.bus_if.synthetic_a", lifetime = 1 : i32, name = "synthetic_a", node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s9.synthetic_a"} {
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.bus_if", is_uninstantiated = false, is_virtual_interface_type_instance = true, name = "bus_if", node_id = 10 : i64, referenced_path = "bus_if", referenced_symbol = @s0.bus_if, sym_name = "s10.bus_if"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.bus_if", name = "bus_if", node_id = 11 : i64, sym_name = "s11.bus_if", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.bus_if.synthetic_b", lifetime = 1 : i32, name = "synthetic_b", node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s12.synthetic_b"} {
            }
          }
        }
      }
    }
  }
}

// CHECK: obelisk_sim.scope.decl 0 hierarchy "\\$root "
// CHECK: obelisk_sim.scope.decl 1 parent 0 hierarchy "top"
// CHECK: obelisk_sim.storage.decl 0 in 1 {{.*}} hierarchy "top.sentinel"
// CHECK-NOT: hierarchy "top.bus_if
// CHECK-NOT: synthetic_a
// CHECK-NOT: synthetic_b
// CHECK-NOT: obelisk.sv.
