// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: sed 's/virtual_interface_access_direction = 1 : i32/virtual_interface_access_direction = 0 : i32/' %s | not obelisk-opt '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=INPUT-WRITE

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32, hierarchical_name = "bus_if", name = "bus_if", node_id = 0 : i64, sym_name = "s0.bus_if"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64, sym_name = "s1.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "s2.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @s1.top, sym_name = "s4.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, sym_name = "s5.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.bus", is_uninstantiated = false, name = "bus", node_id = 6 : i64, referenced_path = "bus_if", referenced_symbol = @s0.bus_if, sym_name = "s6.bus"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.bus", name = "bus_if", node_id = 7 : i64, sym_name = "s7.bus_if", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s12.bus_if} {
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.bus.req", lifetime = 1 : i32, name = "req", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.req"} {}
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.bus.ack", lifetime = 1 : i32, name = "ack", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.ack"} {}
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.vif", lifetime = 1 : i32, name = "vif", node_id = 10 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s12.bus_if, "master">, sym_name = "s10.vif"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 11 : i64, procedure_kind = 0 : i32, sym_name = "s11", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 12 : i64} {
            obelisk.sv.statement.list attributes {node_id = 13 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 15 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s12.bus_if, "master">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s10.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s12.bus_if, "master">} {}
                  obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 17 : i64, referenced_path = "top.bus", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s12.bus_if, "master">} {}
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 19 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "req", node_id = 20 : i64, referenced_path = "top.bus.req", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s8.req, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, virtual_interface_access_direction = 1 : i32, virtual_interface_modport = "master"} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s10.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s12.bus_if, "master">} {}
                  }
                  obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "ack", node_id = 22 : i64, referenced_path = "top.bus.ack", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s9.ack, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, virtual_interface_access_direction = 0 : i32, virtual_interface_modport = "master"} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s10.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s12.bus_if, "master">} {}
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.bus_if", is_uninstantiated = false, is_virtual_interface_type_instance = true, name = "bus_if", node_id = 24 : i64, referenced_path = "bus_if", referenced_symbol = @s0.bus_if, sym_name = "s12.bus_if"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.bus_if", name = "bus_if", node_id = 25 : i64, sym_name = "s13.bus_if", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {}
        }
      }
    }
  }
}

// CHECK: !obelisk_sim.virtual_interface<"@s2.$root::@s5.top::@s12.bus_if", "master">
// CHECK: obelisk_sim.ref.load
// CHECK: obelisk_sim.ref.store
// CHECK-NOT: obelisk.sv.
// INPUT-WRITE: cannot write an input virtual-interface member
