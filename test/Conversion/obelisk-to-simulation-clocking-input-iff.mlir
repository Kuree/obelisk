// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: sed 's/clocking_input_skew_one_step/clocking_input_skew_delay = "0"/' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=ZERO

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.enable", lifetime = 1 : i32, name = "enable", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.enable"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.q", lifetime = 1 : i32, name = "q", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.q"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.sink", lifetime = 1 : i32, name = "sink", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.sink"} {}
        obelisk.sv.symbol.clocking_block attributes {hierarchical_name = "top.cb", is_default = true, is_global = false, name = "cb", node_id = 9 : i64, sym_name = "s9.cb"} {
          obelisk.sv.symbol.clock_var attributes {direction = 0 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb.q", input_edge = 0 : i32, lifetime = 1 : i32, name = "q", node_id = 10 : i64, output_edge = 0 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s10.q"} {}
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 11 : i64, procedure_kind = 0 : i32, sym_name = "s11", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 12 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.sink", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.sink, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
              obelisk.sv.expression.named_value attributes {clocking_access_direction = 0 : i32, clocking_event_edge = 1 : i32, clocking_event_has_iff, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_input_skew_edge = 0 : i32, clocking_input_skew_one_step, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.q", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s7.q, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 15 : i64, referenced_path = "top.cb.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.cb::@s10.q, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 17 : i64, operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "top.enable", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.enable, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "top.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.q, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
                }
              }
            }
          }
        }
      }
    }
  }
}

// The static sampler binds the arbitrary condition locally and updates only
// at qualified clocking events in Observed.
// CHECK-LABEL: simulation.func private @unit_0.$clocking_input.
// CHECK-NOT: !simulation.observer<
// CHECK-COUNT-2: simulation.observer.bind
// CHECK: simulation.ref.load
// CHECK: simulation.suspend.observe
// CHECK-SAME: conditions 1 edges [1] indices [0]
// CHECK-SAME: resume_region = 8 : i32
// CHECK: simulation.assert.sampled_read
// CHECK: simulation.assert.clocked_sample_update
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: simulation.assert.clocked_sample_read
// CHECK-NOT: obelisk.sv.

// ZERO-LABEL: simulation.func private @unit_0.$clocking_input.
// ZERO: simulation.suspend.observe
// ZERO-SAME: resume_region = 8 : i32
// ZERO: simulation.ref.load
// ZERO: simulation.assert.clocked_sample_update
