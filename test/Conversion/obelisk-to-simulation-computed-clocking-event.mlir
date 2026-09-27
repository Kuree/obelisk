// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.enable", lifetime = 1 : i32, name = "enable", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.enable"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.tick", lifetime = 1 : i32, name = "tick", node_id = 23 : i64, semantic_type = !obelisk.event, sym_name = "s9.tick"} {}
        obelisk.sv.symbol.clocking_block attributes {clocking_event_monitor, hierarchical_name = "top.cb", is_default = true, is_global = false, name = "cb", node_id = 7 : i64, sym_name = "s7.cb"} {
          obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 8 : i64} {
            obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 9 : i64, operator_kind = 5 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "top.enable", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.enable, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            }
          }
        }
        obelisk.sv.symbol.clocking_block attributes {clocking_event_monitor, hierarchical_name = "top.cb_named", is_default = false, is_global = false, name = "cb_named", node_id = 24 : i64, sym_name = "s10.cb_named"} {
          obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = true, node_id = 25 : i64} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 26 : i64, referenced_path = "top.tick", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.tick, semantic_type = !obelisk.event} {}
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 27 : i64, referenced_path = "top.enable", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.enable, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 12 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 13 : i64} {
            obelisk.sv.statement.list attributes {node_id = 14 : i64} {
              obelisk.sv.statement.timed attributes {node_id = 15 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 16 : i64} {
                  obelisk.sv.expression.arbitrary_symbol attributes {clocking_block_event, clocking_event_edge = 0 : i32, clocking_event_monitor, clocking_event_path = "top.cb", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s7.cb, is_signed = false, node_id = 17 : i64, referenced_path = "top.cb", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.cb, semantic_type = !obelisk.void} {}
                }
                obelisk.sv.statement.empty attributes {node_id = 18 : i64} {}
              }
              obelisk.sv.statement.timed attributes {node_id = 19 : i64} {
                obelisk.sv.timing.cycle_delay attributes {clocking_event_edge = 0 : i32, clocking_event_monitor, clocking_event_path = "top.cb", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s7.cb, node_id = 20 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 21 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                }
                obelisk.sv.statement.empty attributes {node_id = 22 : i64} {}
              }
              obelisk.sv.statement.timed attributes {node_id = 28 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 29 : i64} {
                  obelisk.sv.expression.arbitrary_symbol attributes {clocking_block_event, clocking_event_edge = 0 : i32, clocking_event_monitor, clocking_event_path = "top.cb_named", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s10.cb_named, is_signed = false, node_id = 30 : i64, referenced_path = "top.cb_named", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.cb_named, semantic_type = !obelisk.void} {}
                }
                obelisk.sv.statement.empty attributes {node_id = 31 : i64} {}
              }
            }
          }
        }
      }
    }
  }
}

// A computed event expression is evaluated once by a shared monitor. All
// consumers wait on the event descriptor published by that monitor.
// CHECK: obelisk_sim.func private @unit_0({{.*}}obelisk_sim.clocking_event_monitor_path = "top.cb"
// CHECK: obelisk_sim.observer.bind
// CHECK: obelisk_sim.suspend.observe %{{.*}} conditions 0 edges [1] indices [-1]
// CHECK: obelisk_sim.event.trigger %{{.*}} nonblocking = false

// Named events with iff use the same monitor path; the event pulse is the
// primary occurrence and the condition gates publication.
// CHECK: obelisk_sim.func private @unit_1({{.*}}obelisk_sim.clocking_event_monitor_path = "top.cb_named"
// CHECK: obelisk_sim.observer.bind {{.*}}schedule.event_primary
// CHECK: obelisk_sim.suspend.observe %{{.*}} conditions 1 edges [0] indices [0]
// CHECK: obelisk_sim.event.trigger %{{.*}} nonblocking = false

// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-COUNT-3: obelisk_sim.suspend.event %{{[^ ]+}} {{.*}}resume_region = 10 : i32
// CHECK-NOT: obelisk.sv.
