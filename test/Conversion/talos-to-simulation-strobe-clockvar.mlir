// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 14.13, 21.2.2: a clocking input read by $strobe is the value
// the clocking block last sampled. The outlined Postponed unit reads it like
// its parent, so it also captures the clock that the shared sampler watches.
//
// module top;
//   bit clk, d;
//   clocking cb @(posedge clk); input #0 d; endclocking
//   initial $strobe("%0d", cb.d);
// endmodule

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
        }
        talos.sv.symbol.variable @s6.d attributes {hierarchical_name = "top.d", lifetime = 1 : i32, name = "d", node_id = 6 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
        }
        talos.sv.symbol.clocking_block @s7.cb attributes {hierarchical_name = "top.cb", is_default = false, is_global = false, name = "cb", node_id = 7 : i64} {
          talos.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 8 : i64} {
            talos.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
            }
          }
          talos.sv.symbol.clock_var @s8.d attributes {direction = 0 : i32, has_input_delay = true, has_output_delay = false, hierarchical_name = "top.cb.d", input_edge = 0 : i32, lifetime = 1 : i32, name = "d", node_id = 10 : i64, output_edge = 0 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
            talos.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.d, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
            }
            talos.sv.timing.delay attributes {node_id = 12 : i64} {
              talos.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 13 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
        talos.sv.symbol.procedural_block @s9 attributes {hierarchical_name = "top", node_id = 14 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 15 : i64} {
            talos.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$strobe", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 16 : i64, semantic_type = !talos.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              talos.sv.expression.string_literal attributes {constant_value = "%0d", is_signed = false, node_id = 17 : i64, semantic_type = !talos.ranged_packed_array<23 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
              }
              talos.sv.expression.named_value attributes {clocking_access_direction = 0 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_input_skew_delay = "0", clocking_input_skew_delay_is_real = false, clocking_input_skew_edge = 0 : i32, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.d", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s6.d, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, is_signed = false, node_id = 18 : i64, referenced_path = "top.cb.d", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.cb::@s8.d, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
              }
            }
          }
        }
      }
    }
  }
}


// CHECK-LABEL: simulation.func private @unit_0.fork.16.0.0.$clocking_input.
// CHECK: simulation.suspend.edge posedge
// CHECK: simulation.assert.clocked_sample_update %arg0 from {{.*}} id [[SITE:[0-9]+]]
// CHECK-LABEL: simulation.func private @unit_0.fork.16.0.0(
// CHECK-SAME: home_region = 16 : i32
// CHECK: simulation.assert.clocked_sample_read %arg0 id [[SITE]]
// CHECK: simulation.display
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: simulation.spawn @unit_0.fork.16.0.0
