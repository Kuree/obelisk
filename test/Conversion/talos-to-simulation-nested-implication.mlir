// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// Source: assert property (@(posedge clk) a |=> (b |-> c));
//
// IEEE 1800-2023 16.12.7: an implication consequent may itself be an
// implication. The nested form fuses into its antecedent, here
// (a ##1 b) |-> c: `b` is gated by `a`'s match on the previous tick, and the
// overlapped inner implication samples `c` on `b`'s tick.
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: simulation.suspend.edge posedge
// CHECK: %[[A:.+]] = simulation.assert.sampled_read %arg0 from %arg2
// CHECK: %[[PENDING:.+]] = arith.cmpi ne
// CHECK: %[[B:.+]] = simulation.assert.sampled_read %arg0 from %arg3
// CHECK-NEXT: %[[ANTECEDENT:.+]] = arith.andi %[[PENDING]], %[[B]]
// CHECK: arith.select %[[A]]
// CHECK: %[[C:.+]] = simulation.assert.sampled_read %arg0 from %arg4
// CHECK-NEXT: arith.andi %[[ANTECEDENT]], %[[C]]

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.m attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s3.m attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 3 : i64, referenced_path = "m", referenced_symbol = @s0.m} {
      talos.sv.symbol.instance_body @s4.m attributes {hierarchical_name = "m", name = "m", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "m", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s5.clk attributes {hierarchical_name = "m.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
        }
        talos.sv.symbol.variable @s6.a attributes {hierarchical_name = "m.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
        }
        talos.sv.symbol.variable @s7.b attributes {hierarchical_name = "m.b", lifetime = 1 : i32, name = "b", node_id = 7 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
        }
        talos.sv.symbol.variable @s8.c attributes {hierarchical_name = "m.c", lifetime = 1 : i32, name = "c", node_id = 8 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
        }
        talos.sv.symbol.procedural_block @s9 attributes {hierarchical_name = "m", node_id = 9 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 10 : i64} {
            talos.sv.assertion.clocking attributes {node_id = 11 : i64} {
              talos.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 12 : i64} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "m.clk", referenced_symbol = @s1.$root::@s3.m::@s4.m::@s5.clk, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                }
              }
              talos.sv.assertion.binary attributes {node_id = 14 : i64, operator_kind = 12 : i32, operator_range = !talos.source_range<"nested-implication.sv", 3, 37, "nested-implication.sv", 3, 40, "">} {
                talos.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 15 : i64, repetition_is_unbounded = false} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "m.a", referenced_symbol = @s1.$root::@s3.m::@s4.m::@s6.a, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                  }
                }
                talos.sv.assertion.binary attributes {node_id = 17 : i64, operator_kind = 11 : i32, operator_range = !talos.source_range<"nested-implication.sv", 3, 44, "nested-implication.sv", 3, 47, "">} {
                  talos.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 18 : i64, repetition_is_unbounded = false} {
                    talos.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "m.b", referenced_symbol = @s1.$root::@s3.m::@s4.m::@s7.b, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                    }
                  }
                  talos.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 20 : i64, repetition_is_unbounded = false} {
                    talos.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "m.c", referenced_symbol = @s1.$root::@s3.m::@s4.m::@s8.c, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                    }
                  }
                }
              }
            }
            talos.sv.statement.empty attributes {node_id = 22 : i64} {
            }
          }
        }
      }
    }
  }
}

