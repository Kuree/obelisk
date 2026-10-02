// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Declared-unsized X/Z numeric literals fill their most-significant unknown
// bit through a wider expression context. An explicitly sized literal with the
// same normalized payload remains zero-extended.

!logic32 = !obelisk.integral<32, false, true, 31 : 0, logic>
!logic68 = !obelisk.integral<68, false, true, 67 : 0, logic>

module {
  simulation.design @unsized_unknown {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.process"

    // CHECK-LABEL: simulation.func @process
    // CHECK: %[[UNSIZED:.*]] = simulation.logic.constant 0 : i32, -1 : i32
    // CHECK: simulation.logic.resize %[[UNSIZED]] signed = true : !simulation.logic<32> -> !simulation.logic<68>
    // CHECK: %[[UNSIZED_Z:.*]] = simulation.logic.constant -1 : i32, -1 : i32
    // CHECK: simulation.logic.resize %[[UNSIZED_Z]] signed = true : !simulation.logic<32> -> !simulation.logic<68>
    // CHECK: %[[SIZED:.*]] = simulation.logic.constant 0 : i32, -1 : i32
    // CHECK: simulation.logic.resize %[[SIZED]] signed = false : !simulation.logic<32> -> !simulation.logic<68>
    // CHECK: %[[KNOWN:.*]] = simulation.logic.constant -2147483648 : i32, 0 : i32
    // CHECK: simulation.logic.resize %[[KNOWN]] signed = false : !simulation.logic<32> -> !simulation.logic<68>
    // CHECK: simulation.return
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.conversion attributes {
            node_id = 2 : i64, semantic_type = !logic68} {
          obelisk.sv.expression.integer_literal attributes {
              constant_value = "32'bxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
              is_declared_unsized = true, node_id = 3 : i64,
              semantic_type = !logic32} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 4 : i64} {
        obelisk.sv.expression.conversion attributes {
            node_id = 5 : i64, semantic_type = !logic68} {
          obelisk.sv.expression.integer_literal attributes {
              constant_value = "32'bzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz",
              is_declared_unsized = true, node_id = 6 : i64,
              semantic_type = !logic32} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 7 : i64} {
        obelisk.sv.expression.conversion attributes {
            node_id = 8 : i64, semantic_type = !logic68} {
          obelisk.sv.expression.integer_literal attributes {
              constant_value = "32'bxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
              node_id = 9 : i64,
              semantic_type = !logic32} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
        obelisk.sv.expression.conversion attributes {
            node_id = 11 : i64, semantic_type = !logic68} {
          obelisk.sv.expression.integer_literal attributes {
              constant_value = "32'b10000000000000000000000000000000",
              is_declared_unsized = true, node_id = 12 : i64,
              semantic_type = !logic32} {
          }
        }
      }
      simulation.return
    }
  }
}
