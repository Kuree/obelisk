// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {}
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.variable @s6.reset_n attributes {hierarchical_name = "top.reset_n", lifetime = 1 : i32, name = "reset_n", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.variable @s7.q attributes {hierarchical_name = "top.q", lifetime = 1 : i32, name = "q", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.variable @s8.sink attributes {hierarchical_name = "top.sink", lifetime = 1 : i32, name = "sink", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.clocking_block @s9.cb attributes {clocking_event_list, hierarchical_name = "top.cb", is_default = true, is_global = false, name = "cb", node_id = 9 : i64} {
          obelisk.sv.timing.event_list attributes {event_count = 2 : i64, node_id = 10 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 11 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            }
            obelisk.sv.timing.signal_event attributes {edge_kind = 2 : i32, has_iff = false, node_id = 13 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "top.reset_n", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.reset_n, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            }
          }
          obelisk.sv.symbol.clock_var @s10.q attributes {direction = 0 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb.q", input_edge = 0 : i32, lifetime = 1 : i32, name = "q", node_id = 15 : i64, output_edge = 0 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "top.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.q, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          }
          obelisk.sv.symbol.clock_var @s11.sink attributes {direction = 1 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb.sink", input_edge = 0 : i32, lifetime = 1 : i32, name = "sink", node_id = 17 : i64, output_edge = 0 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "top.sink", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.sink, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          }
        }
        obelisk.sv.symbol.procedural_block @s12 attributes {hierarchical_name = "top", node_id = 19 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 20 : i64} {
            obelisk.sv.statement.list attributes {node_id = 21 : i64} {
              obelisk.sv.statement.timed attributes {node_id = 22 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 23 : i64} {
                  obelisk.sv.expression.arbitrary_symbol attributes {clocking_block_event, clocking_event_edge = 0 : i32, clocking_event_list, clocking_event_path = "top.cb", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s9.cb, is_signed = false, node_id = 24 : i64, referenced_path = "top.cb", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.cb, semantic_type = !obelisk.void} {}
                }
                obelisk.sv.statement.empty attributes {node_id = 25 : i64} {}
              }
              obelisk.sv.statement.timed attributes {node_id = 26 : i64} {
                obelisk.sv.timing.cycle_delay attributes {clocking_event_edge = 0 : i32, clocking_event_list, clocking_event_path = "top.cb", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s9.cb, node_id = 27 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 28 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                }
                obelisk.sv.statement.empty attributes {node_id = 29 : i64} {}
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 30 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 31 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 32 : i64, referenced_path = "top.sink", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.sink, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  obelisk.sv.expression.named_value attributes {clocking_access_direction = 0 : i32, clocking_event_edge = 0 : i32, clocking_event_list, clocking_event_path = "top.cb", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s9.cb, clocking_input_skew_edge = 0 : i32, clocking_input_skew_one_step, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.q", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s7.q, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, is_signed = false, node_id = 33 : i64, referenced_path = "top.cb.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.cb::@s10.q, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 34 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, is_signed = false, node_id = 35 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 0 : i32, clocking_event_list, clocking_event_path = "top.cb", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s9.cb, clocking_input_skew_edge = 0 : i32, clocking_input_skew_one_step, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.sink", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s8.sink, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, is_signed = false, node_id = 36 : i64, referenced_path = "top.cb.sink", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.cb::@s11.sink, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 37 : i64, referenced_path = "top.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.q, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
              }
            }
          }
        }
      }
    }
  }
}

// One shared event-list monitor owns the mixed clocking event and publishes a
// single event descriptor to every clocking consumer.
// CHECK: %[[EVENT:.*]] = simulation.context.event %{{.*}}[0]
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: [[MON_EVENT:%arg[0-9]+]]: !simulation.event
// CHECK-SAME: simulation.clocking_event_monitor_path = "top.cb"
// CHECK: simulation.suspend.any %{{.*}}, %{{.*}} edges [1, 2]
// CHECK: simulation.event.trigger [[MON_EVENT]] nonblocking = false

// Inputs sample the published occurrence in Observed. Procedural @(cb) and ##
// consume the same event in Reactive.
// CHECK-LABEL: simulation.func private @{{.*}}$clocking_input.{{.*}}(
// CHECK: simulation.suspend.event %{{[^ ]+}} {{.*}}resume_region = 8 : i32

// The drive follows a clocking-event continuation, so zero skew uses the
// current occurrence and enqueues directly in NBA instead of waiting again.
// CHECK-LABEL: simulation.func private @unit_1.$clocking_output.36(
// CHECK-NOT: simulation.suspend
// CHECK: simulation.nba.enqueue
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-COUNT-2: simulation.suspend.event %{{[^ ]+}} {{.*}}resume_region = 10 : i32
// CHECK-NOT: obelisk.sv.
