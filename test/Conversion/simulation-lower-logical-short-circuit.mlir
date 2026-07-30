// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Drives semantic-unit lowering directly from hand-authored MLIR. The right
// operands are assignment expressions, making their side effects easy to
// locate relative to the generated AND and OR short-circuit branches. The OR
// input is wide so its truth value must first be reduced.

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>
!logic4 = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>

module {
  obelisk_sim.design @short_circuit {
    obelisk_sim.code_unit.decl 9100001 in 0 initial
        hierarchy "test.short_circuit.9100001"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1>
        design hierarchy "top.lhs"
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1>
        design hierarchy "top.rhs"
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<1>
        design hierarchy "top.result"
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<4>
        design hierarchy "top.vector"
    obelisk_sim.storage.decl 4 in 0 : !obelisk_sim.logic<1>
        design hierarchy "top.or_result"

    // CHECK-LABEL: obelisk_sim.func @unit
    // CHECK: %[[LHS:.*]] = obelisk_sim.ref.load %arg1
    // CHECK: %[[LHS_PRED:.*]] = obelisk_sim.logic.reduction or %[[LHS]]
    // CHECK: %[[FALSE:.*]] = obelisk_sim.logic.constant false, false
    // CHECK: %[[CONTROL:.*]] = obelisk_sim.logic.compare case_eq %[[LHS_PRED]], %[[FALSE]]
    // CHECK: %[[SHORT_RESULT:.*]] = obelisk_sim.logic.constant false, false
    // CHECK: cf.cond_br %[[CONTROL]], ^[[MERGE:.*]](%[[SHORT_RESULT]] : !obelisk_sim.logic<1>), ^[[RHS:.*]]
    // CHECK: ^[[RHS]]:
    // CHECK: %[[TRUE:.*]] = obelisk_sim.logic.constant true, false
    // CHECK: obelisk_sim.ref.store %[[TRUE]] to %arg2
    // CHECK: %[[RHS_PRED:.*]] = obelisk_sim.logic.reduction or %[[TRUE]]
    // CHECK: %[[LOGICAL:.*]] = obelisk_sim.logic.logical and %[[LHS_PRED]], %[[RHS_PRED]]
    // CHECK: cf.br ^[[MERGE]](%[[LOGICAL]] : !obelisk_sim.logic<1>)
    // CHECK: ^[[MERGE]](%[[VALUE:.*]]: !obelisk_sim.logic<1>):
    // CHECK: obelisk_sim.ref.store %[[VALUE]] to %arg3
    // CHECK: %[[OR_LHS:.*]] = obelisk_sim.ref.load %arg4
    // CHECK: %[[OR_LHS_PRED:.*]] = obelisk_sim.logic.reduction or %[[OR_LHS]]
    // CHECK: %[[OR_CONTROL:.*]] = obelisk_sim.logic.is_true %[[OR_LHS_PRED]]
    // CHECK: %[[OR_SHORT_RESULT:.*]] = obelisk_sim.logic.constant true, false
    // CHECK: cf.cond_br %[[OR_CONTROL]], ^[[OR_MERGE:.*]](%[[OR_SHORT_RESULT]] : !obelisk_sim.logic<1>), ^[[OR_RHS:.*]]
    // CHECK: ^[[OR_RHS]]:
    // CHECK: %[[OR_FALSE:.*]] = obelisk_sim.logic.constant false, false
    // CHECK: obelisk_sim.ref.store %[[OR_FALSE]] to %arg2
    // CHECK: %[[OR_RHS_PRED:.*]] = obelisk_sim.logic.reduction or %[[OR_FALSE]]
    // CHECK: %[[OR_LOGICAL:.*]] = obelisk_sim.logic.logical or %[[OR_LHS_PRED]], %[[OR_RHS_PRED]]
    // CHECK: cf.br ^[[OR_MERGE]](%[[OR_LOGICAL]] : !obelisk_sim.logic<1>)
    // CHECK: ^[[OR_MERGE]](%[[OR_VALUE:.*]]: !obelisk_sim.logic<1>):
    // CHECK: obelisk_sim.ref.store %[[OR_VALUE]] to %arg5
    obelisk_sim.func @unit(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %lhs: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %rhs: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %result: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 2 : i64},
        %vector: !obelisk_sim.ref<!obelisk_sim.logic<4>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 3 : i64},
        %or_result: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 4 : i64})
        attributes {
          entry_kind = 1 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.lhs", argument = 1,
                kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.rhs", argument = 2,
                kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.result", argument = 3,
                kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.vector", argument = 4,
                kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.or_result", argument = 5,
                kind = direct, copyOut = false>
          ],
          code_unit_id = 9100001 : i64
        } {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.assignment attributes {
            node_id = 2 : i64, assignment_kind = 0 : i32,
            semantic_type = !logic1} {
          obelisk.sv.expression.named_value attributes {
              node_id = 3 : i64, referenced_path = "top.result",
              referenced_symbol = @result, semantic_type = !logic1} {
          }
          obelisk.sv.expression.binary_op attributes {
              node_id = 4 : i64, operator_kind = 19 : i32,
              semantic_type = !logic1} {
            obelisk.sv.expression.named_value attributes {
                node_id = 5 : i64, referenced_path = "top.lhs",
                referenced_symbol = @lhs, semantic_type = !logic1} {
            }
            obelisk.sv.expression.assignment attributes {
                node_id = 6 : i64, assignment_kind = 0 : i32,
                semantic_type = !logic1} {
              obelisk.sv.expression.named_value attributes {
                  node_id = 7 : i64, referenced_path = "top.rhs",
                  referenced_symbol = @rhs, semantic_type = !logic1} {
              }
              obelisk.sv.expression.integer_literal attributes {
                  node_id = 8 : i64, constant_value = "1'b1",
                  semantic_type = !logic1} {
              }
            }
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
        obelisk.sv.expression.assignment attributes {
            node_id = 10 : i64, assignment_kind = 0 : i32,
            semantic_type = !logic1} {
          obelisk.sv.expression.named_value attributes {
              node_id = 11 : i64, referenced_path = "top.or_result",
              referenced_symbol = @or_result, semantic_type = !logic1} {
          }
          obelisk.sv.expression.binary_op attributes {
              node_id = 12 : i64, operator_kind = 20 : i32,
              semantic_type = !logic1} {
            obelisk.sv.expression.named_value attributes {
                node_id = 13 : i64, referenced_path = "top.vector",
                referenced_symbol = @vector, semantic_type = !logic4} {
            }
            obelisk.sv.expression.assignment attributes {
                node_id = 14 : i64, assignment_kind = 0 : i32,
                semantic_type = !logic1} {
              obelisk.sv.expression.named_value attributes {
                  node_id = 15 : i64, referenced_path = "top.rhs",
                  referenced_symbol = @rhs, semantic_type = !logic1} {
              }
              obelisk.sv.expression.integer_literal attributes {
                  node_id = 16 : i64, constant_value = "1'b0",
                  semantic_type = !logic1} {
              }
            }
          }
        }
      }
      obelisk_sim.return
    }
  }
}
