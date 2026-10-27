// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s --implicit-check-not=negedge

// IEEE 1800-2023 14.16: the cycle delay on the right-hand side of a
// synchronous drive counts cycles of the target clockvar's own clocking block
// (cb, posedge clk), not of default clocking (negedge slow), and needs no
// default clocking at all.
//
// module top;
//   bit clk, slow, value;
//   clocking cb @(posedge clk); output value; endclocking
//   default clocking dcb @(negedge slow); endclocking
//   initial cb.value <= ##2 1;
// endmodule

// CHECK-LABEL: simulation.func private @unit_0.$clocking_output.
// CHECK: %[[TWO:.+]] = arith.constant 2 : i32
// CHECK: cf.br ^[[WAIT:bb[0-9]+]](%[[TWO]] : i32)
// CHECK: simulation.suspend.edge posedge %arg2 to ^[[RESUME:bb[0-9]+]]
// CHECK: ^[[RESUME]](%[[COUNT:.+]]: i32)
// CHECK: %[[REMAINING:.+]] = arith.subi %[[COUNT]]
// CHECK: cf.cond_br {{.*}}, ^[[WAIT]](%[[REMAINING]] : i32), ^[[DRIVE:bb[0-9]+]]
// CHECK: ^[[DRIVE]]:
// CHECK: simulation.nba.enqueue

// The helper's third argument is cb's clock, top.clk (descriptor 0).
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: %arg1: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}
// CHECK: simulation.spawn @unit_0.$clocking_output.{{[0-9]+}}(%arg0, %{{arg[0-9]+}}, %arg1)

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
        talos.sv.symbol.variable @s6.slow attributes {hierarchical_name = "top.slow", lifetime = 1 : i32, name = "slow", node_id = 6 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
        }
        talos.sv.symbol.variable @s7.value attributes {hierarchical_name = "top.value", lifetime = 1 : i32, name = "value", node_id = 7 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
        }
        talos.sv.symbol.clocking_block @s8.cb attributes {hierarchical_name = "top.cb", is_default = false, is_global = false, name = "cb", node_id = 8 : i64} {
          talos.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 9 : i64} {
            talos.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
            }
          }
          talos.sv.symbol.clock_var @s9.value attributes {direction = 1 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb.value", input_edge = 0 : i32, lifetime = 1 : i32, name = "value", node_id = 11 : i64, output_edge = 0 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
            talos.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "top.value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.value, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
            }
          }
        }
        talos.sv.symbol.clocking_block @s10.dcb attributes {hierarchical_name = "top.dcb", is_default = true, is_global = false, name = "dcb", node_id = 13 : i64} {
          talos.sv.timing.signal_event attributes {edge_kind = 2 : i32, has_iff = false, node_id = 14 : i64} {
            talos.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "top.slow", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.slow, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
            }
          }
        }
        talos.sv.symbol.procedural_block @s11 attributes {hierarchical_name = "top", node_id = 16 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 17 : i64} {
            talos.sv.expression.assignment attributes {assignment_kind = 1 : i32, has_timing_control = true, is_signed = false, node_id = 18 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
              talos.sv.timing.cycle_delay attributes {node_id = 19 : i64} {
                talos.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 20 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                }
              }
              talos.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_input_skew_edge = 0 : i32, clocking_input_skew_one_step, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.value", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s7.value, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, is_signed = false, node_id = 21 : i64, referenced_path = "top.cb.value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.cb::@s9.value, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
              }
              talos.sv.expression.conversion attributes {folded_constant = "1'b1", is_implicit = true, is_signed = false, node_id = 22 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 23 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

