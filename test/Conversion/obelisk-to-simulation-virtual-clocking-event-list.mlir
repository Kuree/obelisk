// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32, hierarchical_name = "bus", name = "bus", node_id = 0 : i64, sym_name = "s0.bus"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64, sym_name = "s1.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "s2.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @s1.top, sym_name = "s4.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, sym_name = "s5.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.b", is_uninstantiated = false, name = "b", node_id = 6 : i64, referenced_path = "bus", referenced_symbol = @s0.bus, sym_name = "s6.b"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.b", name = "bus", node_id = 7 : i64, sym_name = "s7.bus", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s6.b} {
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b.clk", lifetime = 1 : i32, name = "clk", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.clk"} {}
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b.reset_n", lifetime = 1 : i32, name = "reset_n", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.reset_n"} {}
            obelisk.sv.symbol.clocking_block attributes {clocking_event_list, hierarchical_name = "top.b.cb", is_default = false, is_global = false, name = "cb", node_id = 10 : i64, sym_name = "s10.cb"} {
              obelisk.sv.timing.event_list attributes {event_count = 2 : i64, node_id = 11 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = true, node_id = 12 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "top.b.clk", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.b::@s7.bus::@s8.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 48 : i64, referenced_path = "top.b.reset_n", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.b::@s7.bus::@s9.reset_n, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
                obelisk.sv.timing.signal_event attributes {edge_kind = 2 : i32, has_iff = false, node_id = 14 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "top.b.reset_n", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.b::@s7.bus::@s9.reset_n, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
              }
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.other", is_uninstantiated = false, name = "other", node_id = 16 : i64, referenced_path = "bus", referenced_symbol = @s0.bus, sym_name = "s11.other"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.other", name = "bus", node_id = 17 : i64, sym_name = "s12.bus", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s6.b} {
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.other.clk", lifetime = 1 : i32, name = "clk", node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s13.clk"} {}
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.other.reset_n", lifetime = 1 : i32, name = "reset_n", node_id = 19 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s14.reset_n"} {}
            obelisk.sv.symbol.clocking_block attributes {clocking_event_list, hierarchical_name = "top.other.cb", is_default = false, is_global = false, name = "cb", node_id = 20 : i64, sym_name = "s15.cb"} {
              obelisk.sv.timing.event_list attributes {event_count = 2 : i64, node_id = 21 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = true, node_id = 22 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "top.other.clk", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s11.other::@s12.bus::@s13.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 49 : i64, referenced_path = "top.other.reset_n", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s11.other::@s12.bus::@s14.reset_n, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
                obelisk.sv.timing.signal_event attributes {edge_kind = 2 : i32, has_iff = false, node_id = 24 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "top.other.reset_n", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s11.other::@s12.bus::@s14.reset_n, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
              }
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.vif", lifetime = 1 : i32, name = "vif", node_id = 26 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.b, "">, sym_name = "s16.vif"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 27 : i64, procedure_kind = 0 : i32, sym_name = "s17", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 28 : i64} {
            obelisk.sv.statement.list attributes {node_id = 29 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 30 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 31 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.b, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 32 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s16.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.b, "">} {}
                  obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 33 : i64, referenced_path = "top.b", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.b, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.b, "">} {}
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 34 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 35 : i64} {
                  obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "cb", node_id = 36 : i64, referenced_path = "top.bus.cb", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s18.bus::@s21.cb, semantic_type = !obelisk.void, virtual_interface_clock_event_edge = 0 : i32, virtual_interface_clock_event_list, virtual_interface_clock_member = "cb", virtual_interface_clocking_block_event} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 37 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s16.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.b, "">} {}
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 38 : i64} {}
              }
              obelisk.sv.statement.timed attributes {node_id = 51 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = true, node_id = 52 : i64} {
                  obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "cb", node_id = 53 : i64, referenced_path = "top.bus.cb", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s18.bus::@s21.cb, semantic_type = !obelisk.void, virtual_interface_clock_event_edge = 0 : i32, virtual_interface_clock_event_list, virtual_interface_clock_member = "cb", virtual_interface_clocking_block_event} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 54 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s16.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.b, "">} {}
                  }
                  obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "reset_n", node_id = 55 : i64, referenced_path = "top.bus.reset_n", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s18.bus::@s20.reset_n, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 56 : i64, referenced_path = "top.vif", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s16.vif, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.b, "">} {}
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 57 : i64} {}
              }
            }
          }
        }
        obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.bus", is_virtual_interface_type_instance = true, name = "bus", node_id = 39 : i64, sym_name = "s18.bus", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s6.b} {
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.bus.clk", lifetime = 1 : i32, name = "clk", node_id = 40 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s19.clk"} {}
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.bus.reset_n", lifetime = 1 : i32, name = "reset_n", node_id = 41 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s20.reset_n"} {}
          obelisk.sv.symbol.clocking_block attributes {clocking_event_list, hierarchical_name = "top.bus.cb", is_default = false, is_global = false, name = "cb", node_id = 42 : i64, sym_name = "s21.cb"} {
            obelisk.sv.timing.event_list attributes {event_count = 2 : i64, node_id = 43 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = true, node_id = 44 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 45 : i64, referenced_path = "top.bus.clk", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s18.bus::@s19.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 50 : i64, referenced_path = "top.bus.reset_n", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s18.bus::@s20.reset_n, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
              }
              obelisk.sv.timing.signal_event attributes {edge_kind = 2 : i32, has_iff = false, node_id = 46 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 47 : i64, referenced_path = "top.bus.reset_n", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s18.bus::@s20.reset_n, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
              }
            }
          }
        }
      }
    }
  }
}

// Every concrete interface owns an independent event descriptor and monitor.
// CHECK-DAG: simulation.scope.decl 2 {{.*}}hierarchy "top.b"{{.*}}simulation.virtual_interface_clock_events = [{descriptor = 0 : i64, member = "cb"}]
// CHECK-DAG: simulation.scope.decl 3 {{.*}}hierarchy "top.other"{{.*}}simulation.virtual_interface_clock_events = [{descriptor = 1 : i64, member = "cb"}]
// CHECK: simulation.func private @unit_0({{.*}}simulation.clocking_event_monitor_path = "top.b.cb"
// CHECK: simulation.suspend.observe %{{.*}} conditions 1 edges [1, 2] indices [0, -1]
// CHECK: simulation.event.trigger %{{.*}} nonblocking = false
// CHECK: simulation.func private @unit_1({{.*}}simulation.clocking_event_monitor_path = "top.other.cb"
// CHECK: simulation.suspend.observe %{{.*}} conditions 1 edges [1, 2] indices [0, -1]
// CHECK: simulation.event.trigger %{{.*}} nonblocking = false

// Virtual selection multiplexes the two event handles by interface scope and
// waits for the selected occurrence in Reactive.
// The use-site iff gets a compact primary evaluator for the selected event.
// CHECK: observer hierarchy "unit_2.$clocking_event_primary.52"
// CHECK: simulation.event.triggered
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-DAG: %[[EVENT1:.*]] = simulation.context.event %{{.*}}[1]
// CHECK-DAG: %[[EVENT0:.*]] = simulation.context.event %{{.*}}[0]
// CHECK: %[[SCOPE:.*]] = simulation.virtual_interface.scope
// CHECK: cf.cond_br %{{.*}}, ^{{.*}}(%[[EVENT0]] : !simulation.event), ^{{.*}}
// CHECK: simulation.suspend.event %{{[^ ]+}} {{.*}}resume_region = 10 : i32
// CHECK: cf.cond_br %{{.*}}, ^{{.*}}(%[[EVENT1]] : !simulation.event), ^{{.*}}
// The additional iff remains a separate observer over the selected virtual
// interface and gates the published clocking event.
// CHECK: simulation.observer.bind {{.*}}schedule.event_primary
// CHECK: simulation.suspend.observe %{{.*}} conditions 1 edges [0] indices [0]
// CHECK-SAME: resume_region = 10 : i32
// CHECK-NOT: obelisk.sv.
