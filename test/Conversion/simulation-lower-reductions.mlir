// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>
!logic8 = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
!bit1 = !obelisk.integral<1, false, false, 0 : 0, bit>
!bit8 = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>

module {
  simulation.design @reductions {
    simulation.code_unit.decl 9700001 in 0 always_comb
        hierarchy "top.reductions"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 :
        !simulation.packed_array<7 : 0 x !simulation.logic<1>>
        design hierarchy "top.value"
    simulation.storage.decl 1 in 0 :
        !simulation.packed_array<7 : 0 x i1>
        design hierarchy "top.bits"

    // CHECK-LABEL: simulation.func @unit
    // CHECK: simulation.logic.reduction and
    // CHECK: simulation.logic.reduction or
    // CHECK: simulation.logic.reduction xor
    // CHECK: simulation.logic.reduction nand
    // CHECK: simulation.logic.reduction nor
    // CHECK: simulation.logic.reduction xnor
    // CHECK: arith.shrui
    // CHECK: arith.trunci
    simulation.func @unit(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: !simulation.ref<!simulation.packed_array<7 : 0 x !simulation.logic<1>>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %bits: !simulation.ref<!simulation.packed_array<7 : 0 x i1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64})
        attributes {
          entry_kind = 4 : i32,
          simulation.bindings = [
            #simulation.argument_binding<path = "top.value", argument = 1,
                kind = direct, copyOut = false>,
            #simulation.argument_binding<path = "top.bits", argument = 2,
                kind = direct, copyOut = false>
          ],
          code_unit_id = 9700001 : i64
        } {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.unary_op attributes {
            node_id = 2 : i64, operator_kind = 3 : i32,
            semantic_type = !logic1} {
          obelisk.sv.expression.named_value attributes {
              node_id = 3 : i64, referenced_path = "top.value",
              referenced_symbol = @value, semantic_type = !logic8} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 4 : i64} {
        obelisk.sv.expression.unary_op attributes {
            node_id = 5 : i64, operator_kind = 4 : i32,
            semantic_type = !logic1} {
          obelisk.sv.expression.named_value attributes {
              node_id = 6 : i64, referenced_path = "top.value",
              referenced_symbol = @value, semantic_type = !logic8} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 7 : i64} {
        obelisk.sv.expression.unary_op attributes {
            node_id = 8 : i64, operator_kind = 5 : i32,
            semantic_type = !logic1} {
          obelisk.sv.expression.named_value attributes {
              node_id = 9 : i64, referenced_path = "top.value",
              referenced_symbol = @value, semantic_type = !logic8} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {
          node_id = 10 : i64} {
        obelisk.sv.expression.unary_op attributes {
            node_id = 11 : i64, operator_kind = 6 : i32,
            semantic_type = !logic1} {
          obelisk.sv.expression.named_value attributes {
              node_id = 12 : i64, referenced_path = "top.value",
              referenced_symbol = @value, semantic_type = !logic8} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {
          node_id = 13 : i64} {
        obelisk.sv.expression.unary_op attributes {
            node_id = 14 : i64, operator_kind = 7 : i32,
            semantic_type = !logic1} {
          obelisk.sv.expression.named_value attributes {
              node_id = 15 : i64, referenced_path = "top.value",
              referenced_symbol = @value, semantic_type = !logic8} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {
          node_id = 16 : i64} {
        obelisk.sv.expression.unary_op attributes {
            node_id = 17 : i64, operator_kind = 8 : i32,
            semantic_type = !logic1} {
          obelisk.sv.expression.named_value attributes {
              node_id = 18 : i64, referenced_path = "top.value",
              referenced_symbol = @value, semantic_type = !logic8} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {
          node_id = 19 : i64} {
        obelisk.sv.expression.unary_op attributes {
            node_id = 20 : i64, operator_kind = 5 : i32,
            semantic_type = !bit1} {
          obelisk.sv.expression.named_value attributes {
              node_id = 21 : i64, referenced_path = "top.bits",
              referenced_symbol = @bits, semantic_type = !bit8} {
          }
        }
      }
      simulation.return
    }
  }
}
