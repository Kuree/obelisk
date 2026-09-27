// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

// IEEE 1800-2017 11.4.3: unary minus is arithmetic, so any surviving X or Z
// operand bit makes the entire result X. Unary plus remains the operand.

!logic4 = !obelisk.integral<4, false, true, 3 : 0, logic>
!logic8 = !obelisk.integral<8, false, true, 7 : 0, logic>
!logic64 = !obelisk.integral<64, false, true, 63 : 0, logic>
!signed_logic8 = !obelisk.integral<8, true, true, 7 : 0, logic>

module {
  simulation.design @integer_unknown_unary {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.process"

    // CHECK-LABEL: simulation.func @process
    // CHECK: simulation.logic.constant 0 : i8, -1 : i8
    // CHECK: simulation.logic.constant 0 : i8, -1 : i8
    // CHECK: simulation.logic.constant 0 : i64, -1 : i64
    // CHECK: simulation.logic.constant 0 : i64, -1 : i64
    // The X group discarded by the explicit size cannot poison the result.
    // CHECK: simulation.logic.constant 0 : i4, 0 : i4
    // An X that survives the literal but is above the caller width still
    // poisons unary minus before the result is truncated.
    // CHECK: simulation.logic.constant 0 : i4, -1 : i4
    // A surviving partial unknown likewise poisons the complete result.
    // CHECK: simulation.logic.constant 0 : i4, -1 : i4
    // Unary plus preserves Z and a mixed known/X/Z operand exactly.
    // CHECK: simulation.logic.constant -1 : i8, -1 : i8
    // CHECK: simulation.logic.constant 9 : i8, 3 : i8
    // CHECK: simulation.return
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "-8'hz", node_id = 2 : i64,
            semantic_type = !logic8} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 3 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "-8'sb10xz", is_signed = true,
            node_id = 4 : i64, semantic_type = !signed_logic8} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 5 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "-'dx", is_declared_unsized = true,
            node_id = 6 : i64, semantic_type = !logic64} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 7 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "-'hz", is_declared_unsized = true,
            node_id = 8 : i64, semantic_type = !logic64} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "-4'hx0", node_id = 10 : i64,
            semantic_type = !logic4} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "-8'hx0", node_id = 12 : i64,
            semantic_type = !logic4} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "-4'h0x", node_id = 14 : i64,
            semantic_type = !logic4} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "+8'hz", node_id = 16 : i64,
            semantic_type = !logic8} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 17 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "+8'b10xz", node_id = 18 : i64,
            semantic_type = !logic8} {
        }
      }
      simulation.return
    }
  }
}
