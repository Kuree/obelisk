// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 12.7.2 and 9.4.5: a repeat count with any X or Z bit is
// zero, for a repeat loop and for a repeat event control alike.
//
// module top;
//   logic [1:0] c;
//   bit clk;
//   int n, r;
//   initial begin
//     repeat (c) n++;
//     r = repeat (c) @(posedge clk) 1;
//   end
// endmodule

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s5.c attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 5 : i64, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
        }
        talos.sv.symbol.variable @s6.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 6 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
        }
        talos.sv.symbol.variable @s7.n attributes {hierarchical_name = "top.n", lifetime = 1 : i32, name = "n", node_id = 7 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.symbol.variable @s8.r attributes {hierarchical_name = "top.r", lifetime = 1 : i32, name = "r", node_id = 8 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.symbol.procedural_block @s9 attributes {hierarchical_name = "top", node_id = 9 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 10 : i64} {
            talos.sv.statement.list attributes {node_id = 11 : i64} {
              talos.sv.statement.repeat_loop attributes {node_id = 12 : i64} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.c, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
                }
                talos.sv.statement.expression_statement attributes {node_id = 14 : i64} {
                  talos.sv.expression.unary_op attributes {is_signed = true, node_id = 15 : i64, operator_kind = 12 : i32, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    talos.sv.expression.named_value attributes {is_signed = true, node_id = 16 : i64, referenced_path = "top.n", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.n, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              talos.sv.statement.expression_statement attributes {node_id = 17 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, has_timing_control = true, is_signed = true, node_id = 18 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  talos.sv.timing.repeated_event attributes {node_id = 19 : i64} {
                    talos.sv.expression.named_value attributes {is_signed = false, node_id = 20 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.c, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    talos.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 21 : i64} {
                      talos.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                      }
                    }
                  }
                  talos.sv.expression.named_value attributes {is_signed = true, node_id = 23 : i64, referenced_path = "top.r", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.r, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                  talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 24 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
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


// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[LOOP_COUNT:.*]] = simulation.packed.flatten
// CHECK: %[[LOOP_BITS:.*]] = simulation.logic.to_bits %[[LOOP_COUNT]] : !simulation.logic<2> -> i2
// CHECK: %[[LOOP_ROUND:.*]] = simulation.logic.from_bits %[[LOOP_BITS]]
// CHECK: %[[LOOP_KNOWN:.*]] = simulation.logic.compare case_eq %[[LOOP_COUNT]], %[[LOOP_ROUND]]
// CHECK: %[[LOOP_WIDE:.*]] = arith.extui %[[LOOP_BITS]] : i2 to i64
// CHECK: %[[LOOP_N:.*]] = arith.select %[[LOOP_KNOWN]], %[[LOOP_WIDE]], %c0_i64 : i64
// CHECK: cf.br ^{{bb[0-9]+}}(%[[LOOP_N]] : i64)

// CHECK: %[[EVENT_COUNT:.*]] = simulation.packed.flatten
// CHECK: %[[EVENT_BITS:.*]] = simulation.logic.to_bits %[[EVENT_COUNT]]
// CHECK: %[[EVENT_ROUND:.*]] = simulation.logic.from_bits %[[EVENT_BITS]]
// CHECK: %[[EVENT_KNOWN:.*]] = simulation.logic.compare case_eq %[[EVENT_COUNT]], %[[EVENT_ROUND]]
// CHECK: %[[EVENT_WIDE:.*]] = arith.extui %[[EVENT_BITS]] : i2 to i64
// CHECK: %[[EVENT_N:.*]] = arith.select %[[EVENT_KNOWN]], %[[EVENT_WIDE]], %{{.*}} : i64
// CHECK: %[[WAIT:.*]] = arith.cmpi sgt, %[[EVENT_N]]
// CHECK: cf.cond_br %[[WAIT]], ^{{bb[0-9]+}}(%[[EVENT_N]] : i64)
// CHECK: simulation.suspend.edge posedge
