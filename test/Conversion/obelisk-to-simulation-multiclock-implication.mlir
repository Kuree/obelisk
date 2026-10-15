// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 16.13.2: the consequent of a multiclocked overlapped
// implication starts at the nearest tick of its own clock at or after the
// antecedent's end point. The shared occurrence coordinator evaluates it in
// the antecedent's cohort when the consequent clock also ticks there, and
// otherwise keeps the attempt as a pending token for that clock. An
// antecedent that does not match succeeds vacuously.
//
// module top;
//   bit a, b, x, z;
//   assert property (@(posedge a) x |-> @(posedge b) z);
// endmodule

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.a attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.variable @s6.b attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.variable @s7.x attributes {hierarchical_name = "top.x", lifetime = 1 : i32, name = "x", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.variable @s8.z attributes {hierarchical_name = "top.z", lifetime = 1 : i32, name = "z", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.procedural_block @s9 attributes {hierarchical_name = "top", node_id = 9 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 10 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 11 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 12 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 14 : i64, operator_kind = 11 : i32, operator_range = !obelisk.source_range<"../../.claude/jobs/2a837eb9/tmp/t124m.sv", 3, 35, "../../.claude/jobs/2a837eb9/tmp/t124m.sv", 3, 38, "">} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 15 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "top.x", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                }
                obelisk.sv.assertion.clocking attributes {node_id = 17 : i64} {
                  obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 18 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.b, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 20 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "top.z", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.z, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 22 : i64} {
            }
          }
        }
      }
    }
  }
}


// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: simulation.multiclock_sequence_coordinator
// CHECK: ^bb1([[PENDING:%.+]]: i64):
// CHECK: simulation.suspend.clock_set %arg1, %arg2, {{.*}} edges [1, 1]
// CHECK: ^bb2([[TOKENS:%.+]]: i64):
// CHECK: [[X:%.+]] = simulation.assert.sampled_read %arg0 from %arg3
// CHECK: [[Z:%.+]] = simulation.assert.sampled_read %arg0 from %arg4
// Pending consequent attempts are consumed by a b tick and fail unless z.
// CHECK: [[CONSUMED:%.+]] = arith.select %{{.+}}, [[TOKENS]], %{{.+}} : i64
// CHECK: [[PASSED:%.+]] = arith.select [[Z]], [[CONSUMED]]
// CHECK: arith.subi [[CONSUMED]], [[PASSED]]
// A new attempt with x waits unless b ticks in the same cohort.
// CHECK: arith.andi %{{.+}}, [[X]] : i1
