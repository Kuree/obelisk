// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=STEP
// RUN: sed 's/clocking_input_skew_one_step/clocking_input_skew_delay = "0"/' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=ZERO
// RUN: sed 's/clocking_input_skew_one_step/clocking_input_skew_delay = "2"/' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=DELAY
// RUN: sed 's/clocking_input_skew_edge = 0 : i32, clocking_input_skew_one_step/clocking_input_skew_edge = 2 : i32, clocking_input_skew_edge_only/' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=EDGE

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.a"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.b"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.sink", lifetime = 1 : i32, name = "sink", node_id = 8 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s8.sink"} {}
        obelisk.sv.symbol.clocking_block attributes {hierarchical_name = "top.cb", is_default = false, is_global = false, name = "cb", node_id = 9 : i64, sym_name = "s9.cb"} {
          obelisk.sv.symbol.clock_var attributes {direction = 0 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb.pair", input_edge = 0 : i32, lifetime = 1 : i32, name = "pair", node_id = 10 : i64, output_edge = 0 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s10.pair"} {}
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 11 : i64, procedure_kind = 0 : i32, sym_name = "s11", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 12 : i64} {
            obelisk.sv.statement.list attributes {node_id = 13 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 15 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "top.sink", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.sink, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {}
                  obelisk.sv.expression.named_value attributes {clocking_access_direction = 0 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_input_skew_edge = 0 : i32, clocking_input_skew_one_step, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_expression, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 17 : i64, referenced_path = "top.cb.pair", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.cb::@s10.pair, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.concatenation attributes {node_id = 18 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                      obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

// #1step evaluates every source operand from its Preponed snapshot.
// STEP-LABEL: simulation.func private @unit_0.$clocking_input.{{[0-9]+}}
// STEP: simulation.suspend.edge posedge
// STEP: simulation.call @observer_{{[^(]+}}
// STEP: simulation.assert.clocked_sample_update
// STEP: simulation.assert.clocked_sample_read
// STEP-LABEL: simulation.func private @observer_{{[^(]+}}
// STEP-COUNT-2: simulation.assert.sampled_read
// STEP: simulation.logic.concat

// #0 evaluates the composite expression in Observed after the clock edge.
// ZERO-LABEL: simulation.func private @unit_0.$clocking_input.{{[0-9]+}}
// ZERO: simulation.suspend.edge posedge
// ZERO: simulation.call @observer_{{[^(]+}}
// ZERO: simulation.assert.clocked_sample_update
// ZERO-LABEL: simulation.func private @observer_{{[^(]+}}
// ZERO-COUNT-2: simulation.ref.load
// ZERO: simulation.logic.concat

// A positive skew transport-delays changes to the complete expression value.
// DELAY-LABEL: simulation.func private @unit_0.$clocking_input_delay.{{[0-9]+}}
// DELAY: simulation.observer.bind
// DELAY: simulation.call @observer_{{[^(]+}}
// DELAY: simulation.suspend.observe
// DELAY: simulation.spawn @unit_0.$clocking_input_delay.{{[0-9]+}}.commit
// DELAY-LABEL: simulation.func private @unit_0.$clocking_input.{{[0-9]+}}
// DELAY: simulation.assert.clocked_sample_read
// DELAY: simulation.assert.clocked_sample_update

// A distinct edge skew evaluates and retains the expression at that edge.
// EDGE-LABEL: simulation.func private @unit_0.$clocking_input.{{[0-9]+}}
// EDGE: simulation.suspend.edge negedge
// EDGE: simulation.call @observer_{{[^(]+}}
// EDGE: simulation.assert.clocked_sample_update
