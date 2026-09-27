// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: sed 's/clocking_input_skew_one_step/clocking_input_skew_delay = "0"/' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=ZERO
// RUN: sed 's/clocking_input_skew_one_step/clocking_input_skew_delay = "2"/' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=SKEW
// RUN: sed 's/clocking_input_skew_one_step/clocking_input_skew_delay = "1.5", clocking_input_skew_delay_is_real = true/' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=REAL-SKEW
// RUN: sed 's/clocking_input_skew_one_step/clocking_input_skew_delay = "-1"/' %s | not obelisk-opt '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=BAD-SKEW
// RUN: sed 's/clocking_access_direction = 0 : i32, clocking_event_edge/clocking_access_direction = 1 : i32, clocking_event_edge/' %s | not obelisk-opt '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=OUTPUT-READ

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.q", lifetime = 1 : i32, name = "q", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.q"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.r", lifetime = 1 : i32, name = "r", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.r"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.sink_q", lifetime = 1 : i32, name = "sink_q", node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s12.sink_q"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.sink_r", lifetime = 1 : i32, name = "sink_r", node_id = 19 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s13.sink_r"} {}
        obelisk.sv.symbol.clocking_block attributes {hierarchical_name = "top.cb", is_default = false, is_global = false, name = "cb", node_id = 8 : i64, sym_name = "s8.cb"} {
          obelisk.sv.symbol.clock_var attributes {direction = 0 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb.q", input_edge = 0 : i32, lifetime = 1 : i32, name = "q", node_id = 9 : i64, output_edge = 0 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.q"} {}
          obelisk.sv.symbol.clock_var attributes {direction = 0 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb.r", input_edge = 2 : i32, lifetime = 1 : i32, name = "r", node_id = 10 : i64, output_edge = 0 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s10.r"} {}
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 11 : i64, procedure_kind = 0 : i32, sym_name = "s11", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 12 : i64} {
            obelisk.sv.statement.list attributes {node_id = 13 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 20 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "top.sink_q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.sink_q, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  obelisk.sv.expression.named_value attributes {clocking_access_direction = 0 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_input_skew_edge = 0 : i32, clocking_input_skew_one_step, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.q", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s6.q, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 15 : i64, referenced_path = "top.cb.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.cb::@s9.q, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 16 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 22 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "top.sink_r", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s13.sink_r, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  obelisk.sv.expression.named_value attributes {clocking_access_direction = 0 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_input_skew_edge = 2 : i32, clocking_input_skew_edge_only, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.r", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s7.r, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 17 : i64, referenced_path = "top.cb.r", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.cb::@s10.r, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
              }
            }
          }
        }
      }
    }
  }
}

// #1step reads the source's Preponed snapshot at each clocking event.
// CHECK: simulation.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK: simulation.assert.sampled_read
// CHECK: simulation.assert.clocked_sample_update

// An edge-only input skew retains the value sampled at the most recent
// selected edge, rather than sampling again at the block's event edge.
// CHECK: simulation.suspend.edge negedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK: simulation.ref.load
// CHECK: simulation.assert.clocked_sample_update

// Both source-level reads use their retained sampled values.
// CHECK-COUNT-2: simulation.assert.clocked_sample_read
// CHECK-NOT: obelisk.sv.

// ZERO: simulation.suspend.edge posedge
// ZERO: simulation.ref.load
// ZERO: simulation.assert.clocked_sample_update
// A positive skew maintains a transport-delayed mirror of the source. Delayed
// changes publish after the event's Observed sampling boundary, so an event
// exactly at source-change-plus-skew still sees the Preponed source value.
// SKEW-LABEL: simulation.func private @unit_0.$clocking_input_delay.{{[0-9]+}}.commit
// SKEW: simulation.time.constant 2{{$|[^0-9]}}
// SKEW: simulation.suspend.delay
// SKEW-SAME: resume_region = 16 : i32
// SKEW: simulation.assert.clocked_sample_update
// SKEW-LABEL: simulation.func private @unit_0.$clocking_input_delay.{{[0-9]+}}(
// SKEW: simulation.assert.sampled_read
// SKEW: simulation.suspend.change
// SKEW: simulation.ref.load
// SKEW: simulation.spawn @unit_0.$clocking_input_delay.{{[0-9]+}}.commit
// SKEW-LABEL: simulation.func private @unit_0.$clocking_input.{{[0-9]+}}
// SKEW: simulation.suspend.edge posedge
// SKEW: simulation.assert.clocked_sample_read
// SKEW: simulation.assert.clocked_sample_update
// SKEW-NOT: obelisk.sv.

// Real skews round to the clocking block's timeprecision.
// REAL-SKEW: simulation.time.constant 2{{$|[^0-9]}}
// BAD-SKEW: clocking input skew is not a known nonnegative value
// OUTPUT-READ: cannot read an output clocking variable
