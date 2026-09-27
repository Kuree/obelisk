// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

// A semantic conversion from a packed string literal to `string` preserves
// the spelling directly. The empty literal has an 8-bit packed source type,
// but must not become a one-byte NUL string.

module {
  simulation.design @string_literal_conversion {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.process"

    // CHECK-LABEL: simulation.func @process
    // CHECK: simulation.string.literal ""
    // CHECK-NOT: simulation.string.from_packed
    // CHECK: simulation.string.literal "text"
    // CHECK-NOT: simulation.string.from_packed
    // CHECK: simulation.return
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.conversion attributes {
            node_id = 2 : i64, semantic_type = !obelisk.string} {
          obelisk.sv.expression.string_literal attributes {
              constant_value = "", node_id = 3 : i64,
              semantic_type = !obelisk.ranged_packed_array<
                  7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 4 : i64} {
        obelisk.sv.expression.conversion attributes {
            node_id = 5 : i64, semantic_type = !obelisk.string} {
          obelisk.sv.expression.string_literal attributes {
              constant_value = "text", node_id = 6 : i64,
              semantic_type = !obelisk.ranged_packed_array<
                  31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
          }
        }
      }
      simulation.return
    }
  }
}
