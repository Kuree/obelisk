// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {}
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.variable @s6.declared_gate attributes {hierarchical_name = "top.declared_gate", lifetime = 1 : i32, name = "declared_gate", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.variable @s7.extra_gate attributes {hierarchical_name = "top.extra_gate", lifetime = 1 : i32, name = "extra_gate", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.variable @s10.q attributes {hierarchical_name = "top.q", lifetime = 1 : i32, name = "q", node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.variable @s13.r attributes {hierarchical_name = "top.r", lifetime = 1 : i32, name = "r", node_id = 25 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        obelisk.sv.symbol.clocking_block @s8.cb attributes {clocking_event_monitor, hierarchical_name = "top.cb", is_default = true, is_global = false, name = "cb", node_id = 8 : i64} {
          obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = true, node_id = 9 : i64} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "top.declared_gate", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.declared_gate, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          }
          obelisk.sv.symbol.clock_var @s11.q attributes {direction = 1 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb.q", input_edge = 0 : i32, lifetime = 1 : i32, name = "q", node_id = 19 : i64, output_edge = 2 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          obelisk.sv.symbol.clock_var @s14.r attributes {direction = 0 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb.r", input_edge = 2 : i32, lifetime = 1 : i32, name = "r", node_id = 26 : i64, output_edge = 0 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
        }
        obelisk.sv.symbol.procedural_block @s9 attributes {hierarchical_name = "top", node_id = 12 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 13 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = true, node_id = 14 : i64} {
              obelisk.sv.expression.arbitrary_symbol attributes {clocking_block_event, clocking_event_edge = 0 : i32, clocking_event_monitor, clocking_event_path = "top.cb", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s8.cb, is_signed = false, node_id = 15 : i64, referenced_path = "top.cb", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.cb, semantic_type = !obelisk.void} {}
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "top.extra_gate", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.extra_gate, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            }
            obelisk.sv.statement.empty attributes {node_id = 17 : i64} {}
          }
        }
        obelisk.sv.symbol.procedural_block @s12 attributes {hierarchical_name = "top", node_id = 20 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 21 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, node_id = 22 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 0 : i32, clocking_event_monitor, clocking_event_path = "top.cb", clocking_event_raw_edge = 1 : i32, clocking_event_raw_path = "top.clk", clocking_event_raw_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s8.cb, clocking_output_skew_edge = 2 : i32, clocking_output_skew_edge_only, clocking_source_path = "top.q", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s10.q, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 23 : i64, referenced_path = "top.cb.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.cb::@s11.q, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
              obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 24 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s15 attributes {hierarchical_name = "top", node_id = 27 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 28 : i64} {
            obelisk.sv.expression.named_value attributes {clocking_access_direction = 0 : i32, clocking_event_edge = 0 : i32, clocking_event_monitor, clocking_event_path = "top.cb", clocking_event_raw_edge = 1 : i32, clocking_event_raw_path = "top.clk", clocking_event_raw_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s8.cb, clocking_input_skew_edge = 2 : i32, clocking_input_skew_edge_only, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.r", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s13.r, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 29 : i64, referenced_path = "top.cb.r", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.cb::@s14.r, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
          }
        }
      }
    }
  }
}

// The declaration publishes only clock edges accepted by its own iff.
// CHECK-DAG: observer hierarchy "top.$code_unit_12.$observer.16.clocking_event_iff"
// CHECK: simulation.func private @unit_0(
// CHECK: simulation.suspend.edge_iff posedge
// CHECK: simulation.event.trigger %{{.*}} nonblocking = false

// The use-site iff observes the published event and applies its independent
// condition before resuming in Reactive.
// CHECK: observer hierarchy "unit_1.$clocking_event_primary.14"
// CHECK: simulation.event.triggered
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK: simulation.observer.bind {{.*}}schedule.event_primary
// CHECK: simulation.observer.bind
// CHECK: simulation.suspend.observe %{{.*}} conditions 1 edges [0] indices [0]
// CHECK-SAME: resume_region = 10 : i32

// An asynchronous distinct-edge output first waits for the qualified clocking
// occurrence, then waits for the selected raw-signal edge before driving.
// CHECK-LABEL: simulation.func private @unit_2.$clocking_output.23
// CHECK: simulation.suspend.event
// CHECK: simulation.suspend.edge negedge
// CHECK: simulation.nba.enqueue

// A distinct input edge is sampled directly from the raw clock, independently
// of whether the later posedge qualifies as a clocking-block occurrence.
// CHECK-LABEL: simulation.func private @unit_3.$clocking_input.{{[0-9]+}}
// CHECK: simulation.suspend.edge negedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK: simulation.assert.clocked_sample_update
// CHECK-NOT: obelisk.sv.
