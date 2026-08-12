// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32, hierarchical_name = "bus_if", name = "bus_if", node_id = 0 : i64, sym_name = "s0.bus_if"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64, sym_name = "s1.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "s2.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @s1.top, sym_name = "s4.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, sym_name = "s5.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.bus", is_uninstantiated = false, name = "bus", node_id = 6 : i64, referenced_path = "bus_if", referenced_symbol = @s0.bus_if, sym_name = "s6.bus"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.bus", name = "bus_if", node_id = 7 : i64, sym_name = "s7.bus_if", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s9.bus_if} {
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.bus.signal", lifetime = 1 : i32, name = "signal", node_id = 23 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s12.signal"} {}
            obelisk.sv.symbol.net attributes {hierarchical_name = "top.bus.ready", is_implicit = false, name = "ready", net_kind = 1 : i32, node_id = 24 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s13.ready"} {}
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.other", is_uninstantiated = false, name = "other", node_id = 50 : i64, referenced_path = "bus_if", referenced_symbol = @s0.bus_if, sym_name = "s14.other"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.other", name = "bus_if", node_id = 51 : i64, sym_name = "s15.bus_if", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s9.bus_if} {
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.other.signal", lifetime = 1 : i32, name = "signal", node_id = 52 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s16.signal"} {}
            obelisk.sv.symbol.net attributes {hierarchical_name = "top.other.ready", is_implicit = false, name = "ready", net_kind = 1 : i32, node_id = 53 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s17.ready"} {}
          }
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
              obelisk.sv.statement.expression_statement attributes {node_id = 25 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 26 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "signal", node_id = 27 : i64, referenced_path = "top.bus_if.signal", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s12.signal, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 28 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 29 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 30 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {}
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 31 : i64} {
                obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "signal", node_id = 32 : i64, referenced_path = "top.bus_if.signal", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s12.signal, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 33 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 34 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 35 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "signal", node_id = 36 : i64, referenced_path = "top.bus_if.signal", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s12.signal, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 37 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
                  }
                  obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "ready", node_id = 38 : i64, referenced_path = "top.bus_if.ready", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s13.ready, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 39 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 40 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 41 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 42 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 43 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {
                    obelisk.sv.expression.null_literal attributes {is_signed = false, node_id = 44 : i64, semantic_type = !obelisk.null} {}
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 60 : i64, procedure_kind = 3 : i32, sym_name = "s18", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 61 : i64} {
            obelisk.sv.statement.list attributes {node_id = 62 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 63 : i64} {
                obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "ready", node_id = 64 : i64, referenced_path = "top.bus_if.ready", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.bus::@s7.bus_if::@s13.ready, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 65 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s8.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s9.bus_if, "">} {}
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

// CHECK: obelisk_sim.scope.decl [[BUS:[0-9]+]] parent 1 hierarchy "top.bus" debug "bus_if" interface "@s2.$root::@s5.top::@s9.bus_if"
// CHECK: obelisk_sim.scope.decl [[OTHER:[0-9]+]] parent 1 hierarchy "top.other" debug "bus_if" interface "@s2.$root::@s5.top::@s9.bus_if"
// CHECK-DAG: obelisk_sim.storage.decl [[SIGNAL:[0-9]+]] in [[BUS]] : !obelisk_sim.logic<1> design hierarchy "top.bus.signal"
// CHECK-DAG: obelisk_sim.net.decl [[READY:[0-9]+]] in [[BUS]] : !obelisk_sim.logic<1> design hierarchy "top.bus.ready"
// CHECK-DAG: obelisk_sim.storage.decl [[OTHER_SIGNAL:[0-9]+]] in [[OTHER]] : !obelisk_sim.logic<1> design hierarchy "top.other.signal"
// CHECK-DAG: obelisk_sim.net.decl [[OTHER_READY:[0-9]+]] in [[OTHER]] : !obelisk_sim.logic<1> design hierarchy "top.other.ready"
// CHECK: obelisk_sim.storage.decl {{[0-9]+}} in 1 : !obelisk_sim.virtual_interface<"@s2.$root::@s5.top::@s9.bus_if", ""> design hierarchy "top.vif"
// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK-DAG: obelisk_sim.virtual_interface.scope
// CHECK-DAG: obelisk_sim.context.storage %arg0{{.*}}[[SIGNAL]]
// CHECK-DAG: obelisk_sim.context.storage %arg0{{.*}}[[OTHER_SIGNAL]]
// CHECK-DAG: obelisk_sim.ref.store
// CHECK-DAG: obelisk_sim.ref.load
// CHECK-DAG: obelisk_sim.context.net %arg0{{.*}}[[READY]]
// CHECK-DAG: obelisk_sim.context.net %arg0{{.*}}[[OTHER_READY]]
// CHECK-DAG: obelisk_sim.net.read
// CHECK-DAG: virtual interface member access used a null or invalid handle.
// CHECK-LABEL: obelisk_sim.func private
// CHECK-SAME: entry_kind = 4 : i32
// CHECK-DAG: %[[READY_HANDLE:.*]] = obelisk_sim.context.net %arg0{{.*}}[[READY]]
// CHECK-DAG: %[[OTHER_READY_HANDLE:.*]] = obelisk_sim.context.net %arg0{{.*}}[[OTHER_READY]]
// The handle itself and both possible member nets participate in implicit
// sensitivity; the final two operands are loop-carried rematerializations.
// CHECK: obelisk_sim.suspend.any {{.*}} edges [0, 0, 0]
// CHECK-NOT: obelisk.sv.
