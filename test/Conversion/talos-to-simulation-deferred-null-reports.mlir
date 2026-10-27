// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 16.4.1: a deferred assertion queues its action block's
// subroutine call, or $error for a failing assert or assume without an else
// clause. A result with no call has no pending report, so it enqueues and
// spawns nothing:
//   always_comb begin
//     assert final (a) else $error("a low");   // node 12: fail only
//     cover final (b) $display("b high");      // node 17: pass only
//     assert #0 (a | b);                       // node 22: null pass action
//   end

// The process advances a private epoch on every resume, a flush point
// (16.4.2); each report captures it, and its evaluator matures only while the
// epoch is unchanged. A native activation resumes without the runtime seeing
// it, so the runtime's own process queue flush alone cannot cancel a report.

// CHECK: simulation.storage.decl [[EPOCH:[0-9]+]] in 1 : i32 design hierarchy "deferred_reports.$deferred_epoch_9"
// CHECK-NOT: simulation.func private @unit_0.fork.12.{{[0-9]+}}.0(
// CHECK-NOT: simulation.func private @unit_0.fork.17.{{[0-9]+}}.1(
// CHECK-NOT: simulation.func private @unit_0.fork.22.{{[0-9]+}}.0(
// CHECK-LABEL: simulation.func private @unit_0.fork.12.{{[0-9]+}}.1(
// CHECK: %[[CURRENT:.*]] = simulation.assert.deferred_mature
// CHECK-NEXT: %[[REF:.*]] = simulation.context.storage %arg0[[[EPOCH]]]
// CHECK-NEXT: %[[NOW:.*]] = simulation.ref.load %[[REF]]
// CHECK-NEXT: %[[SAME:.*]] = arith.cmpi eq, %[[NOW]], %arg3
// CHECK-NEXT: arith.andi %[[CURRENT]], %[[SAME]]
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: ^bb1:
// CHECK-NEXT: %[[FLUSH:.*]] = simulation.context.storage %arg0[[[EPOCH]]]
// CHECK-NEXT: %[[OLD:.*]] = simulation.ref.load %[[FLUSH]]
// CHECK: %[[NEXT:.*]] = arith.addi %[[OLD]]
// CHECK-NEXT: simulation.ref.store %[[NEXT]] to %[[FLUSH]]
// CHECK: simulation.assert.deferred_enqueue {{[0-9]+}} {simulation.deferred_epoch_storage = [[EPOCH]] : i64}
// CHECK: simulation.spawn @unit_0.fork.12.{{[0-9]+}}.1(
// CHECK: simulation.assert.deferred_enqueue
// CHECK: simulation.spawn @unit_0.fork.17.{{[0-9]+}}.0(
// CHECK: simulation.assert.deferred_enqueue
// CHECK: simulation.spawn @unit_0.fork.22.{{[0-9]+}}.1(
// CHECK-NOT: simulation.assert.deferred_enqueue
// CHECK: simulation.suspend.any {{.*}} to ^bb1

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.deferred_reports attributes {definition_kind = 0 : i32, hierarchical_name = "deferred_reports", name = "deferred_reports", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    talos.sv.symbol.instance @s3.deferred_reports attributes {hierarchical_name = "deferred_reports", is_uninstantiated = false, name = "deferred_reports", node_id = 3 : i64, referenced_path = "deferred_reports", referenced_symbol = @s0.deferred_reports} {
      talos.sv.symbol.instance_body @s4.deferred_reports attributes {hierarchical_name = "deferred_reports", name = "deferred_reports", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.port @s5.a attributes {direction = 0 : i32, hierarchical_name = "deferred_reports.a", name = "a", node_id = 5 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
        talos.sv.symbol.variable @s6.a attributes {hierarchical_name = "deferred_reports.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
        talos.sv.symbol.port @s7.b attributes {direction = 0 : i32, hierarchical_name = "deferred_reports.b", name = "b", node_id = 7 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
        talos.sv.symbol.variable @s8.b attributes {hierarchical_name = "deferred_reports.b", lifetime = 1 : i32, name = "b", node_id = 8 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
        talos.sv.symbol.procedural_block @s9 attributes {hierarchical_name = "deferred_reports", node_id = 9 : i64, procedure_kind = 3 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 10 : i64} {
            talos.sv.statement.list attributes {node_id = 11 : i64} {
              talos.sv.statement.immediate_assertion attributes {assertion_kind = 0 : i32, has_fail_action = true, has_pass_action = false, is_deferred = true, is_final = true, node_id = 12 : i64} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "deferred_reports.a", referenced_symbol = @s1.$root::@s3.deferred_reports::@s4.deferred_reports::@s6.a, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                }
                talos.sv.statement.expression_statement attributes {node_id = 14 : i64} {
                  talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$error", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 15 : i64, semantic_type = !talos.void, subroutine_kind = 0 : i32, system_library_cell = "work.deferred_reports", system_scope_path = "deferred_reports", system_scope_symbol = @s1.$root::@s3.deferred_reports::@s4.deferred_reports} {
                    talos.sv.expression.string_literal attributes {constant_value = "a low", is_signed = false, node_id = 16 : i64, semantic_type = !talos.ranged_packed_array<39 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              talos.sv.statement.immediate_assertion attributes {assertion_kind = 2 : i32, has_fail_action = false, has_pass_action = true, is_deferred = true, is_final = true, node_id = 17 : i64} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "deferred_reports.b", referenced_symbol = @s1.$root::@s3.deferred_reports::@s4.deferred_reports::@s8.b, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                }
                talos.sv.statement.expression_statement attributes {node_id = 19 : i64} {
                  talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 20 : i64, semantic_type = !talos.void, subroutine_kind = 1 : i32, system_library_cell = "work.deferred_reports", system_scope_path = "deferred_reports", system_scope_symbol = @s1.$root::@s3.deferred_reports::@s4.deferred_reports} {
                    talos.sv.expression.string_literal attributes {constant_value = "b high", is_signed = false, node_id = 21 : i64, semantic_type = !talos.ranged_packed_array<47 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              talos.sv.statement.immediate_assertion attributes {assertion_kind = 0 : i32, has_fail_action = false, has_pass_action = true, is_deferred = true, is_final = false, node_id = 22 : i64} {
                talos.sv.expression.binary_op attributes {is_signed = false, node_id = 23 : i64, operator_kind = 6 : i32, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "deferred_reports.a", referenced_symbol = @s1.$root::@s3.deferred_reports::@s4.deferred_reports::@s6.a, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                  }
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "deferred_reports.b", referenced_symbol = @s1.$root::@s3.deferred_reports::@s4.deferred_reports::@s8.b, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                talos.sv.statement.empty attributes {node_id = 26 : i64} {
                }
              }
            }
          }
        }
      }
    }
  }
}

