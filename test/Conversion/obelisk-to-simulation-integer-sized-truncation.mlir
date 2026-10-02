// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

// IEEE 1800-2017 5.7.1: when a based literal's digit value is wider than its
// explicit size, discard high bits. Apply that declared size before a consumer
// widens the value, preserving the based literal's signedness.

!bit3 = !obelisk.integral<3, false, false, 2 : 0, bit>
!bit4 = !obelisk.integral<4, false, false, 3 : 0, bit>
!bit5 = !obelisk.integral<5, false, false, 4 : 0, bit>
!bit8 = !obelisk.integral<8, false, false, 7 : 0, bit>
!bit64 = !obelisk.integral<64, false, false, 63 : 0, bit>
!logic8 = !obelisk.integral<8, false, true, 7 : 0, logic>
!signed_bit4 = !obelisk.integral<4, true, false, 3 : 0, bit>
!signed_bit8 = !obelisk.integral<8, true, false, 7 : 0, bit>
!signed_logic8 = !obelisk.integral<8, true, true, 7 : 0, logic>

module {
  simulation.design @integer_sized_truncation {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.process"

    // CHECK-LABEL: simulation.func @process
    // CHECK: arith.constant -1 : i4
    // CHECK: arith.constant -1 : i5
    // CHECK: arith.constant 3 : i3
    // CHECK: arith.constant -1 : i4
    // CHECK: arith.constant -1 : i4
    // CHECK: arith.constant 0 : i4
    // CHECK: arith.constant -1 : i4
    // Unary signs apply after declared-width truncation.
    // CHECK: arith.constant 1 : i4
    // CHECK: arith.constant 1 : i4
    // CHECK: arith.constant -1 : i4
    // A wider consumer extends from the declared sign bit.
    // CHECK: arith.constant 8 : i8
    // CHECK: arith.constant -8 : i8
    // CHECK: simulation.logic.constant 0 : i8, 15 : i8
    // CHECK: simulation.logic.constant 0 : i8, -1 : i8
    // A huge declared width must not cause a proportional allocation.
    // CHECK: arith.constant 31 : i64
    // CHECK: %[[FIFTEEN:.*]] = simulation.time.constant 15
    // CHECK: simulation.suspend.delay %[[FIFTEEN]]
    // CHECK: %[[ZERO:.*]] = simulation.time.constant 0
    // CHECK: simulation.suspend.delay %[[ZERO]]
    // CHECK: simulation.return
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64,
                    simulation.delay_scale = 1 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "4'h1f", node_id = 2 : i64,
            semantic_type = !bit4} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 3 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "5'O77", node_id = 4 : i64,
            semantic_type = !bit5} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 5 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "3'b1011", node_id = 6 : i64,
            semantic_type = !bit3} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 7 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "4'd31", node_id = 8 : i64,
            semantic_type = !bit4} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "4'sd31", is_signed = true, node_id = 10 : i64,
            semantic_type = !signed_bit4} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "4'd32", node_id = 12 : i64,
            semantic_type = !bit4} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "4'H1_F", node_id = 14 : i64,
            semantic_type = !bit4} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "-4'd31", node_id = 24 : i64,
            semantic_type = !bit4} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 25 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "-4'sd15", is_signed = true, node_id = 26 : i64,
            semantic_type = !signed_bit4} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 27 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "+4'd31", node_id = 28 : i64,
            semantic_type = !bit4} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "4'h8", node_id = 30 : i64,
            semantic_type = !bit8} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 31 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "4'sh8", is_signed = true, node_id = 32 : i64,
            semantic_type = !signed_bit8} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 33 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "4'hx", node_id = 34 : i64,
            semantic_type = !logic8} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 35 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "4'shx", is_signed = true, node_id = 36 : i64,
            semantic_type = !signed_logic8} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 37 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "4294967295'h1f", node_id = 38 : i64,
            semantic_type = !bit64} {
        }
      }
      obelisk.sv.statement.timed attributes {node_id = 15 : i64} {
        obelisk.sv.timing.delay attributes {node_id = 16 : i64} {
          obelisk.sv.expression.integer_literal attributes {
              constant_value = "4'h1f", node_id = 17 : i64,
              semantic_type = !bit4} {
          }
        }
        obelisk.sv.statement.empty attributes {node_id = 18 : i64} {
        }
      }
      obelisk.sv.statement.timed attributes {node_id = 19 : i64} {
        obelisk.sv.timing.delay attributes {node_id = 20 : i64} {
          obelisk.sv.expression.integer_literal attributes {
              constant_value = "4'sh1f", is_signed = true, node_id = 21 : i64,
              semantic_type = !signed_bit4} {
          }
        }
        obelisk.sv.statement.empty attributes {node_id = 22 : i64} {
        }
      }
      simulation.return
    }
  }
}
