// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

// IEEE 1800-2017 5.7.1: a decimal based literal may contain one X, Z,
// or ? digit, which sets every bit. For every nondecimal base, an X/Z
// leftmost digit also supplies the fill for unwritten high bits.

!logic5 = !obelisk.integral<5, false, true, 4 : 0, logic>
!logic8 = !obelisk.integral<8, false, true, 7 : 0, logic>
!logic10 = !obelisk.integral<10, false, true, 9 : 0, logic>
!logic12 = !obelisk.integral<12, false, true, 11 : 0, logic>
!signed_logic16 = !obelisk.integral<16, true, true, 15 : 0, logic>

module {
  simulation.design @integer_unknown_fill {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.process"

    // CHECK-LABEL: simulation.func @process
    // CHECK: simulation.logic.constant 0 : i8, -1 : i8
    // CHECK: simulation.logic.constant -1 : i12, -1 : i12
    // CHECK: simulation.logic.constant -1 : i5, -1 : i5
    // CHECK: simulation.logic.constant 5 : i12, -16 : i12
    // CHECK: simulation.logic.constant -11 : i12, -16 : i12
    // CHECK: simulation.logic.constant -5 : i10, -8 : i10
    // CHECK: simulation.logic.constant 0 : i8, -1 : i8
    // CHECK: simulation.logic.constant -1 : i8, -1 : i8
    // CHECK: simulation.logic.constant 15 : i5, -1 : i5
    // CHECK: simulation.logic.constant 80 : i12, 15 : i12
    // CHECK: simulation.logic.constant -1 : i16, -1 : i16
    // CHECK: simulation.return
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "8'dx", node_id = 2 : i64,
            semantic_type = !logic8} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 3 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "12'DZ_", node_id = 4 : i64,
            semantic_type = !logic12} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 5 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "5'd?", node_id = 6 : i64,
            semantic_type = !logic5} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 7 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "12'hx5", node_id = 8 : i64,
            semantic_type = !logic12} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "12'hz5", node_id = 10 : i64,
            semantic_type = !logic12} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "10'o?3", node_id = 12 : i64,
            semantic_type = !logic10} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "8'bx", node_id = 14 : i64,
            semantic_type = !logic8} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "8'bz", node_id = 16 : i64,
            semantic_type = !logic8} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 17 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "5'hx?", node_id = 18 : i64,
            semantic_type = !logic5} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 19 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "12'h5x", node_id = 20 : i64,
            semantic_type = !logic12} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 21 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "16'sd?", is_signed = true, node_id = 22 : i64,
            semantic_type = !signed_logic16} {
        }
      }
      simulation.return
    }
  }
}
