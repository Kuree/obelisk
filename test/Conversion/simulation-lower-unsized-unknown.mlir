// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Declared-unsized X/Z numeric literals fill their most-significant unknown
// bit through a wider expression context. An explicitly sized literal with the
// same normalized payload remains zero-extended.

!logic32 = !obelisk.integral<32, false, true, 31 : 0, logic>
!logic68 = !obelisk.integral<68, false, true, 67 : 0, logic>

module {
  obelisk_sim.design @unsized_unknown {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.process"

    // CHECK-LABEL: obelisk_sim.func @process
    // CHECK: %[[UNSIZED:.*]] = obelisk_sim.logic.constant 0 : i32, -1 : i32
    // CHECK: obelisk_sim.logic.resize %[[UNSIZED]] signed = true : !obelisk_sim.logic<32> -> !obelisk_sim.logic<68>
    // CHECK: %[[UNSIZED_Z:.*]] = obelisk_sim.logic.constant -1 : i32, -1 : i32
    // CHECK: obelisk_sim.logic.resize %[[UNSIZED_Z]] signed = true : !obelisk_sim.logic<32> -> !obelisk_sim.logic<68>
    // CHECK: %[[SIZED:.*]] = obelisk_sim.logic.constant 0 : i32, -1 : i32
    // CHECK: obelisk_sim.logic.resize %[[SIZED]] signed = false : !obelisk_sim.logic<32> -> !obelisk_sim.logic<68>
    // CHECK: %[[KNOWN:.*]] = obelisk_sim.logic.constant -2147483648 : i32, 0 : i32
    // CHECK: obelisk_sim.logic.resize %[[KNOWN]] signed = false : !obelisk_sim.logic<32> -> !obelisk_sim.logic<68>
    // CHECK: obelisk_sim.return
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
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
      obelisk_sim.return
    }
  }
}
