// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

// An inout $random seed is elaborated with an implicit conversion to the
// system function's `int` argument type. The conversion must not hide the
// writable reference. IEEE 1800-2017 20.15.1 and normative Annex N require
// the legacy distribution algorithm and its next seed to be stored back;
// $random must not perturb the separate process-local $urandom stream.

!integer = !obelisk.integral<32, true, true, 31 : 0, integer>
!int = !obelisk.integral<32, true, false, 31 : 0, int>

module {
  simulation.design @random_seed {
    simulation.code_unit.decl 9940001 in 0 initial hierarchy "top.random_seed"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<32> design hierarchy "top.seed"
    simulation.storage.decl 1 in 0 : !simulation.logic<32> design hierarchy "top.value"
    simulation.storage.decl 2 in 0 : !simulation.logic<32> design hierarchy "top.default_value"

    // CHECK-LABEL: simulation.func @run
    // CHECK: %[[OLD:.*]] = simulation.ref.load %arg1
    // CHECK: %[[BITS:.*]] = simulation.logic.to_bits %[[OLD]] : !simulation.logic<32> -> i32
    // CHECK-NOT: simulation.random.seed
    // CHECK-NOT: simulation.random.next
    // CHECK: %[[DRAW:.*]], %[[NEXT_SEED:.*]] = simulation.random.distribution %arg0, %[[BITS]], {{.*}}, {{.*}} {distribution = #simulation.random_distribution<uniform>}
    // CHECK: %[[UPDATED_SEED:.*]] = simulation.logic.from_bits %[[NEXT_SEED]] : i32 -> !simulation.logic<32>
    // CHECK: simulation.ref.store %[[UPDATED_SEED]] to %arg1
    // CHECK: %[[RESULT:.*]] = simulation.logic.from_bits %[[DRAW]] : i32 -> !simulation.logic<32>
    // CHECK: simulation.ref.store %[[RESULT]] to %arg2
    // CHECK: %[[DEFAULT:.*]] = simulation.random.legacy %arg0 : (!simulation.context) -> i32
    // CHECK: %[[DEFAULT_RESULT:.*]] = simulation.logic.from_bits %[[DEFAULT]] : i32 -> !simulation.logic<32>
    // CHECK: simulation.ref.store %[[DEFAULT_RESULT]] to %arg3
    simulation.func @run(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %seed: !simulation.ref<!simulation.logic<32>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %value: !simulation.ref<!simulation.logic<32>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64},
        %default_value: !simulation.ref<!simulation.logic<32>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64})
        attributes {
          entry_kind = 1 : i32,
          simulation.bindings = [
            #simulation.argument_binding<path = "top.seed", argument = 1,
                kind = direct, copyOut = false>,
            #simulation.argument_binding<path = "top.value", argument = 2,
                kind = direct, copyOut = false>,
            #simulation.argument_binding<path = "top.default_value", argument = 3,
                kind = direct, copyOut = false>
          ],
          code_unit_id = 9940001 : i64
        } {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.assignment attributes {
            assignment_kind = 0 : i32, is_signed = true, node_id = 2 : i64,
            semantic_type = !integer} {
          obelisk.sv.expression.named_value attributes {
              is_signed = true, node_id = 3 : i64,
              referenced_path = "top.value", referenced_symbol = @value,
              semantic_type = !integer} {
          }
          obelisk.sv.expression.conversion attributes {
              is_implicit = true, is_signed = true, node_id = 4 : i64,
              semantic_type = !integer} {
            obelisk.sv.expression.call attributes {
                argument_count = 1 : i64, callee_name = "$random",
                constraint_restrictions = [], defaulted_arguments = array<i64: 0>,
                has_inline_constraints = false, has_iterator_expression = false,
                has_output_arguments = false, has_this_class = false,
                is_signed = true, is_super_class = false, is_system_call = true,
                node_id = 5 : i64, semantic_type = !int,
                subroutine_kind = 0 : i32} {
              obelisk.sv.expression.conversion attributes {
                  is_implicit = true, is_signed = true, node_id = 6 : i64,
                  semantic_type = !int} {
                obelisk.sv.expression.named_value attributes {
                    is_signed = true, node_id = 7 : i64,
                    referenced_path = "top.seed", referenced_symbol = @seed,
                    semantic_type = !integer} {
                }
              }
            }
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
        obelisk.sv.expression.assignment attributes {
            assignment_kind = 0 : i32, is_signed = true, node_id = 9 : i64,
            semantic_type = !integer} {
          obelisk.sv.expression.named_value attributes {
              is_signed = true, node_id = 10 : i64,
              referenced_path = "top.default_value",
              referenced_symbol = @default_value,
              semantic_type = !integer} {
          }
          obelisk.sv.expression.conversion attributes {
              is_implicit = true, is_signed = true, node_id = 11 : i64,
              semantic_type = !integer} {
            obelisk.sv.expression.call attributes {
                argument_count = 0 : i64, callee_name = "$random",
                constraint_restrictions = [], defaulted_arguments = array<i64>,
                has_inline_constraints = false, has_iterator_expression = false,
                has_output_arguments = false, has_this_class = false,
                is_signed = true, is_super_class = false, is_system_call = true,
                node_id = 12 : i64, semantic_type = !int,
                subroutine_kind = 0 : i32} {
            }
          }
        }
      }
      simulation.return
    }
  }
}
