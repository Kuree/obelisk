// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32, hierarchical_name = "bus_if", name = "bus_if", node_id = 0 : i64, sym_name = "s0.bus_if"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64, sym_name = "s1.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "s2.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @s1.top, sym_name = "s4.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, sym_name = "s5.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.bus", is_uninstantiated = false, name = "bus", node_id = 6 : i64, referenced_path = "bus_if", referenced_symbol = @s0.bus_if, sym_name = "s6.bus"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.bus", name = "bus_if", node_id = 7 : i64, sym_name = "s7.bus_if", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {}
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.vif", lifetime = 1 : i32, name = "vif", node_id = 8 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">, sym_name = "s8.vif"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 9 : i64, procedure_kind = 0 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 10 : i64} {
            obelisk.sv.statement.list attributes {node_id = 11 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 12 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
                  obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 15 : i64, referenced_path = "top.bus", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 16 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 17 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 19 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {
                    obelisk.sv.expression.null_literal attributes {is_signed = false, node_id = 20 : i64, semantic_type = !obelisk.null} {}
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.bus_if", is_uninstantiated = false, is_virtual_interface_type_instance = true, name = "bus_if", node_id = 21 : i64, referenced_path = "bus_if", referenced_symbol = @s0.bus_if, sym_name = "s9.bus_if"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.bus_if", name = "bus_if", node_id = 22 : i64, sym_name = "s11.bus_if", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {}
        }
      }
    }
  }
}

// CHECK: obelisk_sim.scope.decl [[BUS:[0-9]+]] parent 1 hierarchy "top.bus"
// CHECK: obelisk_sim.storage.decl {{[0-9]+}} in 1 : !obelisk_sim.virtual_interface<"@s2.$root::@s5.top::@s9.bus_if", ""> design hierarchy "top.vif"
// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: %[[NULL:.*]] = obelisk_sim.virtual_interface.null
// CHECK: %[[BUS_HANDLE:.*]] = obelisk_sim.virtual_interface.bind [[BUS]]
// CHECK: obelisk_sim.ref.store %[[BUS_HANDLE]] to %arg1
// CHECK: obelisk_sim.ref.store %[[NULL]] to %arg1
// CHECK-NOT: obelisk.sv.
