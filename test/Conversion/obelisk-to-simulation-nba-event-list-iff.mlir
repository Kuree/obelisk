// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 9.4.2 and 9.4.5: an intra-assignment event control accepts
// the same event-expression list and iff qualifiers as a procedural wait.

module {
  obelisk.sv.symbol.definition @s0.nba_event_list_iff attributes {definition_kind = 0 : i32, hierarchical_name = "nba_event_list_iff", name = "nba_event_list_iff", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.nba_event_list_iff attributes {hierarchical_name = "nba_event_list_iff", is_uninstantiated = false, name = "nba_event_list_iff", node_id = 3 : i64, referenced_path = "nba_event_list_iff", referenced_symbol = @s0.nba_event_list_iff} {
      obelisk.sv.symbol.instance_body @s4.nba_event_list_iff attributes {hierarchical_name = "nba_event_list_iff", name = "nba_event_list_iff", node_id = 4 : i64} {
        obelisk.sv.symbol.variable @s5.clk attributes {hierarchical_name = "nba_event_list_iff.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s6.reset attributes {hierarchical_name = "nba_event_list_iff.reset", lifetime = 1 : i32, name = "reset", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s7.enable attributes {hierarchical_name = "nba_event_list_iff.enable", lifetime = 1 : i32, name = "enable", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s8.lhs attributes {hierarchical_name = "nba_event_list_iff.lhs", lifetime = 1 : i32, name = "lhs", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s9.rhs attributes {hierarchical_name = "nba_event_list_iff.rhs", lifetime = 1 : i32, name = "rhs", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.procedural_block @s10 attributes {hierarchical_name = "nba_event_list_iff", node_id = 10 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, has_timing_control = true, node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.timing.event_list attributes {event_count = 2 : i64, node_id = 13 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = true, node_id = 14 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "nba_event_list_iff.clk", referenced_symbol = @s1.$root::@s3.nba_event_list_iff::@s4.nba_event_list_iff::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "nba_event_list_iff.enable", referenced_symbol = @s1.$root::@s3.nba_event_list_iff::@s4.nba_event_list_iff::@s7.enable, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.timing.signal_event attributes {edge_kind = 2 : i32, has_iff = false, node_id = 17 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 18 : i64, referenced_path = "nba_event_list_iff.reset", referenced_symbol = @s1.$root::@s3.nba_event_list_iff::@s4.nba_event_list_iff::@s6.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
              obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "nba_event_list_iff.lhs", referenced_symbol = @s1.$root::@s3.nba_event_list_iff::@s4.nba_event_list_iff::@s8.lhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_path = "nba_event_list_iff.rhs", referenced_symbol = @s1.$root::@s3.nba_event_list_iff::@s4.nba_event_list_iff::@s9.rhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-LABEL: simulation.func private @{{.*nba_event.*}}(
// CHECK-SAME: schedule.detached_controls
// CHECK-SAME: schedule.prime_on_spawn
// CHECK: %[[CLK:.*]] = simulation.observer.bind
// CHECK: %[[ENABLE:.*]] = simulation.observer.bind
// CHECK: %[[RESET:.*]] = simulation.observer.bind
// CHECK: simulation.suspend.observe %[[CLK]], %[[RESET]]
// CHECK-SAME: conditions 1 edges [1, 2] indices [0, -1]
// CHECK: simulation.nba.enqueue
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[RHS:.*]] = simulation.ref.load
// CHECK: simulation.spawn @{{.*nba_event.*}}(%{{.*}}, %{{.*}}, %[[RHS]]
// CHECK-NOT: obelisk.sv.
