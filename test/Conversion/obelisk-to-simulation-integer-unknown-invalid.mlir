// RUN: not obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' 2>&1 | FileCheck %s

// IEEE 1800-2017 5.7.1 permits an X/Z/? in a decimal literal only when it is
// the sole digit. Keep the shared parser defensive for hand-authored IR.

!logic8 = !obelisk.integral<8, false, true, 7 : 0, logic>

module {
  simulation.design @integer_unknown_invalid {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.process"
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        // CHECK: error: invalid decimal X/Z integer literal '8'd1x'
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "8'd1x", node_id = 2 : i64,
            semantic_type = !logic8} {
        }
      }
      simulation.return
    }
  }
}

// -----

// A simple decimal number contains only decimal digits. X/Z/? requires a
// based spelling (or the separate unbased-unsized literal syntax).

!logic8_bare = !obelisk.integral<8, false, true, 7 : 0, logic>

module {
  simulation.design @integer_unknown_bare_invalid {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.process"
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        // CHECK: error: invalid decimal X/Z integer literal 'x'
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "x", node_id = 2 : i64,
            semantic_type = !logic8_bare} {
        }
      }
      simulation.return
    }
  }
}
