// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: sed 's/clocking_input_skew_one_step/clocking_input_skew_delay = "0"/' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=ZERO
// RUN: sed 's/clocking_input_skew_one_step/clocking_input_skew_delay = "2"/' %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=SKEW

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.data", lifetime = 1 : i32, name = "data", node_id = 6 : i64, semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "s6.data"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.sink", lifetime = 1 : i32, name = "sink", node_id = 7 : i64, semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "s7.sink"} {}
        obelisk.sv.symbol.clocking_block attributes {hierarchical_name = "top.cb", is_default = false, is_global = false, name = "cb", node_id = 8 : i64, sym_name = "s8.cb"} {
          obelisk.sv.symbol.clock_var attributes {direction = 0 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb.data", input_edge = 0 : i32, lifetime = 1 : i32, name = "data", node_id = 9 : i64, output_edge = 0 : i32, semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "s9.data"} {}
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 0 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 11 : i64} {
            obelisk.sv.statement.list attributes {node_id = 12 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 14 : i64, semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "top.sink", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.sink, semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {}
                  obelisk.sv.expression.named_value attributes {clocking_access_direction = 0 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_input_skew_edge = 0 : i32, clocking_input_skew_one_step, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.data", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s6.data, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 16 : i64, referenced_path = "top.cb.data", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.cb::@s9.data, semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {}
                }
              }
            }
          }
        }
      }
    }
  }
}

// Each fixed unpacked element is sampled independently at the Preponed
// boundary and retained independently at the clocking event.
// CHECK-COUNT-2: simulation.assert.sampled_read
// CHECK-COUNT-2: simulation.assert.clocked_sample_update
// The source-level read reconstructs the declared array. The sampler's
// temporary construction is folded into its two leaf updates.
// CHECK-COUNT-2: simulation.assert.clocked_sample_read
// CHECK: simulation.aggregate.construct
// CHECK-NOT: obelisk.sv.

// A #0 sample may load the aggregate in Observed, but retention remains one
// independent ring per packed leaf.
// ZERO: simulation.ref.load
// ZERO-COUNT-2: simulation.assert.clocked_sample_update
// ZERO-COUNT-2: simulation.assert.clocked_sample_read
// ZERO: simulation.aggregate.construct
// ZERO-NOT: obelisk.sv.

// A positive skew likewise decomposes both its delayed mirror and its final
// clocking-input ring, so no aggregate value is aliased across time slots.
// SKEW: simulation.suspend.delay
// SKEW-COUNT-4: simulation.assert.clocked_sample_update
// SKEW-COUNT-4: simulation.assert.clocked_sample_read
// SKEW-NOT: obelisk.sv.
