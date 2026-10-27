// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s
// RUN: talos-opt %s '--test-talos-edit-attributes=match={clocking_input_skew_one_step} remove=clocking_input_skew_one_step set={clocking_input_skew_delay = \"0\"}' '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=ZERO

module {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {}
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        talos.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {}
        talos.sv.symbol.variable @s6.enable attributes {hierarchical_name = "top.enable", lifetime = 1 : i32, name = "enable", node_id = 6 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {}
        talos.sv.symbol.variable @s7.q attributes {hierarchical_name = "top.q", lifetime = 1 : i32, name = "q", node_id = 7 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {}
        talos.sv.symbol.variable @s8.sink attributes {hierarchical_name = "top.sink", lifetime = 1 : i32, name = "sink", node_id = 8 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {}
        talos.sv.symbol.clocking_block @s9.cb attributes {hierarchical_name = "top.cb", is_default = true, is_global = false, name = "cb", node_id = 9 : i64} {
          talos.sv.symbol.clock_var @s10.q attributes {direction = 0 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb.q", input_edge = 0 : i32, lifetime = 1 : i32, name = "q", node_id = 10 : i64, output_edge = 0 : i32, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {}
        }
        talos.sv.symbol.procedural_block @s11 attributes {hierarchical_name = "top", node_id = 11 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 12 : i64} {
            talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 13 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
              talos.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.sink", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.sink, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {}
              talos.sv.expression.named_value attributes {clocking_access_direction = 0 : i32, clocking_event_edge = 1 : i32, clocking_event_has_iff, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_input_skew_edge = 0 : i32, clocking_input_skew_one_step, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.q", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s7.q, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, node_id = 15 : i64, referenced_path = "top.cb.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.cb::@s10.q, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {}
                talos.sv.expression.binary_op attributes {is_signed = false, node_id = 17 : i64, operator_kind = 19 : i32, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "top.enable", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.enable, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {}
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "top.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.q, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {}
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
// CHECK-NOT: talos.sv.

// ZERO-LABEL: simulation.func private @unit_0.$clocking_input.
// ZERO: simulation.suspend.observe
// ZERO-SAME: resume_region = 8 : i32
// ZERO: simulation.ref.load
// ZERO: simulation.assert.clocked_sample_update
