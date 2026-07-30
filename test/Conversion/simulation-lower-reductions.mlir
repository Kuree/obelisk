// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>
!logic8 = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
!bit1 = !obelisk.integral<1, false, false, 0 : 0, bit>
!bit8 = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>

module {
  obelisk_sim.design @reductions {
    obelisk_sim.code_unit.decl 9700001 in 0 always_comb
        hierarchy "top.reductions"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 :
        !obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>
        design hierarchy "top.value"
    obelisk_sim.storage.decl 1 in 0 :
        !obelisk_sim.packed_array<7 : 0 x i1>
        design hierarchy "top.bits"

    // CHECK-LABEL: obelisk_sim.func @unit
    // CHECK: obelisk_sim.logic.reduction and
    // CHECK: obelisk_sim.logic.reduction or
    // CHECK: obelisk_sim.logic.reduction xor
    // CHECK: obelisk_sim.logic.reduction nand
    // CHECK: obelisk_sim.logic.reduction nor
    // CHECK: obelisk_sim.logic.reduction xnor
    // CHECK: arith.shrui
    // CHECK: arith.trunci
    obelisk_sim.func @unit(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<!obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %bits: !obelisk_sim.ref<!obelisk_sim.packed_array<7 : 0 x i1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {
          entry_kind = 4 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.value", argument = 1,
                kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.bits", argument = 2,
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
      obelisk_sim.return
    }
  }
}
