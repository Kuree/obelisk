// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 22.9 requires `unconnected_drive to provide an implicit
// pull value for otherwise unconnected module input ports.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unconnected_drive_top", name = "unconnected_drive_top", node_id = 0 : i64, sym_name = "s0.unconnected_drive_top"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unconnected_drive_child", name = "unconnected_drive_child", node_id = 1 : i64, sym_name = "s1.unconnected_drive_child"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "s2.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unconnected_drive_top", is_uninstantiated = false, name = "unconnected_drive_top", node_id = 4 : i64, referenced_path = "unconnected_drive_top", referenced_symbol = @s0.unconnected_drive_top, sym_name = "s4.unconnected_drive_top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unconnected_drive_top", name = "unconnected_drive_top", node_id = 5 : i64, sym_name = "s5.unconnected_drive_top"} {
        obelisk.sv.symbol.instance attributes {hierarchical_name = "unconnected_drive_top.child", is_uninstantiated = false, name = "child", node_id = 6 : i64, referenced_path = "unconnected_drive_child", referenced_symbol = @s1.unconnected_drive_child, sym_name = "s6.child"} {
          obelisk.sv.port.connection attributes {actual_is_constant = true, direction = 0 : i32, formal_name = "pull0", formal_ordinal = 0 : i64, formal_path = "unconnected_drive_top.child.pull0", formal_symbol = @s2.$root::@s4.unconnected_drive_top::@s5.unconnected_drive_top::@s6.child::@s7.unconnected_drive_child::@s8.pull0, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "unconnected_drive_top.child.pull0", internal_symbol = @s2.$root::@s4.unconnected_drive_top::@s5.unconnected_drive_top::@s6.child::@s7.unconnected_drive_child::@s9.pull0, is_ansi = true, is_net = true, node_id = 7 : i64, provenance = 4 : i32, unconnected_drive_value = false} {
          } {
          }
          obelisk.sv.port.connection attributes {actual_is_constant = true, direction = 0 : i32, formal_name = "pull1", formal_ordinal = 1 : i64, formal_path = "unconnected_drive_top.child.pull1", formal_symbol = @s2.$root::@s4.unconnected_drive_top::@s5.unconnected_drive_top::@s6.child::@s7.unconnected_drive_child::@s10.pull1, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "unconnected_drive_top.child.pull1", internal_symbol = @s2.$root::@s4.unconnected_drive_top::@s5.unconnected_drive_top::@s6.child::@s7.unconnected_drive_child::@s11.pull1, is_ansi = true, is_net = true, node_id = 8 : i64, provenance = 4 : i32, unconnected_drive_value = true} {
          } {
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unconnected_drive_top.child", name = "unconnected_drive_child", node_id = 9 : i64, sym_name = "s7.unconnected_drive_child"} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "unconnected_drive_top.child.pull0", name = "pull0", node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.pull0"} {
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "unconnected_drive_top.child.pull0", is_implicit = false, name = "pull0", net_kind = 1 : i32, node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.pull0"} {
            }
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "unconnected_drive_top.child.pull1", name = "pull1", node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s10.pull1"} {
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "unconnected_drive_top.child.pull1", is_implicit = false, name = "pull1", net_kind = 1 : i32, node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s11.pull1"} {
            }
          }
        }
      }
    }
  }
}

// CHECK-DAG: obelisk_sim.code_unit.decl {{[0-9]+}} in {{[0-9]+}} port_initialize hierarchy "unconnected_drive_top.child.$port_connection_0"
// CHECK-DAG: obelisk_sim.code_unit.decl {{[0-9]+}} in {{[0-9]+}} port_initialize hierarchy "unconnected_drive_top.child.$port_connection_1"
// CHECK-DAG: obelisk_sim.driver.decl {{[0-9]+}} in {{[0-9]+}} drives {{[0-9]+}} : !obelisk_sim.logic<1> design hierarchy "unconnected_drive_top.child.pull0"
// CHECK-DAG: obelisk_sim.driver.decl {{[0-9]+}} in {{[0-9]+}} drives {{[0-9]+}} : !obelisk_sim.logic<1> design hierarchy "unconnected_drive_top.child.pull1"
// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: obelisk_sim.logic.constant false, false
// CHECK: obelisk_sim.driver.drive
// CHECK-LABEL: obelisk_sim.func private @unit_1
// CHECK: obelisk_sim.logic.constant true, false
// CHECK: obelisk_sim.driver.drive
// CHECK-NOT: obelisk.sv.
