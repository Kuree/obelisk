// RUN: not obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' 2>&1 | FileCheck %s

// Unsized literals still have to agree with their semantic width; only an
// explicitly sized based literal receives the LRM's left truncation.

!bit4 = !obelisk.integral<4, false, false, 3 : 0, bit>

module {
  obelisk_sim.design @integer_unsized_overflow {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.process"
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        // CHECK: error: integer literal '32' does not fit in 4 bits
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "32", node_id = 2 : i64,
            semantic_type = !bit4} {
        }
      }
      obelisk_sim.return
    }
  }
}

// -----

// The internal APInt width is represented by unsigned. Reject a size token
// outside that representation instead of narrowing it.

!bit4_huge = !obelisk.integral<4, false, false, 3 : 0, bit>

module {
  obelisk_sim.design @integer_unrepresentable_size {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.process"
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        // CHECK: error: invalid size in SystemVerilog integer literal '4294967296'h1'
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "4294967296'h1", node_id = 2 : i64,
            semantic_type = !bit4_huge} {
        }
      }
      obelisk_sim.return
    }
  }
}

// -----

// Section 5.7.1 requires a nonzero unsigned size token.

!bit4_zero = !obelisk.integral<4, false, false, 3 : 0, bit>

module {
  obelisk_sim.design @integer_zero_size {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.process"
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        // CHECK: error: invalid size in SystemVerilog integer literal '0'h1'
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "0'h1", node_id = 2 : i64,
            semantic_type = !bit4_zero} {
        }
      }
      obelisk_sim.return
    }
  }
}
