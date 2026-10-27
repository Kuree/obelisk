// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s
// RUN: talos-opt %s '--test-talos-edit-attributes=match={operator_kind = 11 : i32} set={operator_kind = 14 : i32}' '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=FOLLOWED
// RUN: not talos-opt %s '--test-talos-edit-attributes=match={node_id = 12 : i64} set={has_repetition = true, repetition_kind = 0 : i32, repetition_min = 1 : i64, repetition_max = 2 : i64}' '--lower-talos-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=BRANCHING

// IEEE 1800-2023 16.17: expect evaluates one attempt of any property. The
// monitor walks `enable |-> ready` (and `enable #=# ready`) one sampled age
// at a time.

module {
  talos.sv.symbol.definition @s0.expect_implication attributes {definition_kind = 0 : i32, hierarchical_name = "expect_implication", name = "expect_implication", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    talos.sv.symbol.instance @s3.expect_implication attributes {hierarchical_name = "expect_implication", is_uninstantiated = false, name = "expect_implication", node_id = 3 : i64, referenced_path = "expect_implication", referenced_symbol = @s0.expect_implication} {
      talos.sv.symbol.instance_body @s4.expect_implication attributes {hierarchical_name = "expect_implication", name = "expect_implication", node_id = 4 : i64} {
        talos.sv.symbol.variable @s5.enable attributes {hierarchical_name = "expect_implication.enable", lifetime = 1 : i32, name = "enable", node_id = 5 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
        talos.sv.symbol.variable @s16.ready attributes {hierarchical_name = "expect_implication.ready", lifetime = 1 : i32, name = "ready", node_id = 16 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
        talos.sv.symbol.procedural_block @s6 attributes {hierarchical_name = "expect_implication", node_id = 6 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.concurrent_assertion attributes {assertion_kind = 5 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 7 : i64} {
            talos.sv.assertion.clocking attributes {node_id = 8 : i64} {
              talos.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 9 : i64} {
                talos.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "expect_implication.enable", referenced_symbol = @s1.$root::@s3.expect_implication::@s4.expect_implication::@s5.enable, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              talos.sv.assertion.binary attributes {node_id = 11 : i64, operator_kind = 11 : i32} {
                talos.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 12 : i64, repetition_is_unbounded = false} {
                  talos.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "expect_implication.enable", referenced_symbol = @s1.$root::@s3.expect_implication::@s4.expect_implication::@s5.enable, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                talos.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 14 : i64, repetition_is_unbounded = false} {
                  talos.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "expect_implication.ready", referenced_symbol = @s1.$root::@s3.expect_implication::@s4.expect_implication::@s16.ready, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
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

// An antecedent mismatch is a vacuous success for implication. While the
// antecedent is pending, an end-of-simulation completion is also a vacuous
// success; once it matches, the default strong consequent decides.
// CHECK-LABEL: simulation.func private @unit_0.fork.7.0.16(
// CHECK: %[[PENDING:.+]] = simulation.ref.alloc %true
// CHECK: simulation.spawn @unit_0.fork.7.0.16.$expect_eos.7({{.*}}, %[[PENDING]])
// CHECK: {{^ *}}^[[PASS:bb[0-9]+]](
// CHECK: {{^ *}}^[[FAIL:bb[0-9]+]](
// CHECK: %[[ANTECEDENT:.+]] = simulation.logic.is_true
// CHECK-NEXT: cf.cond_br %[[ANTECEDENT]], ^[[CONSEQUENT:bb[0-9]+]]({{.*}}), ^[[PASS]](
// CHECK: {{^ *}}^[[CONSEQUENT]](
// CHECK-NEXT: %[[STRONG:.+]] = arith.constant {{.*}}false
// CHECK-NEXT: simulation.ref.store %[[STRONG]]
// CHECK-NOT: simulation.suspend
// CHECK: %[[MATCH:.+]] = simulation.logic.is_true
// CHECK-NEXT: cf.cond_br %[[MATCH]], ^[[PASS]](
// CHECK-SAME: ^[[FAIL]](
// CHECK-LABEL: simulation.func private @unit_0.fork.7.0.16.$expect_eos.7(
// CHECK: %[[RESULT:.+]] = simulation.ref.load %arg5
// CHECK-NEXT: simulation.ref.store %[[RESULT]] to %arg2

// Followed-by fails when its antecedent does not match or is still pending,
// and #=# waits one clock before its consequent.
// FOLLOWED-LABEL: simulation.func private @unit_0.fork.7.0.16(
// FOLLOWED: %[[PENDING:.+]] = simulation.ref.alloc %false
// FOLLOWED: {{^ *}}^[[PASS:bb[0-9]+]](
// FOLLOWED: {{^ *}}^[[FAIL:bb[0-9]+]](
// FOLLOWED: %[[ANTECEDENT:.+]] = simulation.logic.is_true
// FOLLOWED-NEXT: cf.cond_br %[[ANTECEDENT]], ^[[HANDOFF:bb[0-9]+]]({{.*}}), ^[[FAIL]](
// FOLLOWED: {{^ *}}^[[HANDOFF]](
// FOLLOWED-NEXT: %[[STRONG:.+]] = arith.constant {{.*}}false
// FOLLOWED-NEXT: simulation.ref.store %[[STRONG]]
// FOLLOWED-NEXT: simulation.suspend.edge posedge

// One expect attempt does not yet walk a branching antecedent such as
// enable[*1:2].
// BRANCHING: branching implication/followed-by antecedents currently require a concurrent directive without locals, expect
