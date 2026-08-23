// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// IEEE 1800-2017 5.7.1: a based literal without an explicit size is at
// least 32 bits, but grows to retain the significant written digits. Leading
// known zeroes do not increase that self-determined width; a leading X/Z
// contributes no significant bits but retains all following digit groups.

!bit32 = !obelisk.integral<32, false, false, 31 : 0, bit>
!logic64 = !obelisk.integral<64, false, true, 63 : 0, logic>
!signed_bit64 = !obelisk.integral<64, true, false, 63 : 0, bit>
!signed_logic64 = !obelisk.integral<64, true, true, 63 : 0, logic>

module {
  obelisk_sim.design @integer_unsized_based {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.width"

    // CHECK-LABEL: obelisk_sim.func @width
    // A signed hexadecimal literal sign-extends from its 32-bit minimum.
    // CHECK: arith.constant -1 : i64
    // CHECK: arith.constant -2147483648 : i64
    // Signed decimal magnitude needs a 33rd sign bit and remains positive.
    // CHECK: arith.constant 4294967295 : i64
    // The literal first grows to 33 bits, then a narrower context truncates it.
    // CHECK: arith.constant -1 : i32
    // A known leading sign bit above 32 unknown low bits extends from bit 35.
    // CHECK: obelisk_sim.logic.constant -4294967296 : i64, 4294967295 : i64
    // A leading X group above 32 lower bits contributes no additional width.
    // CHECK: obelisk_sim.logic.constant 0 : i64, 0 : i64
    // An unsized high X/Z fills through the wider expression context.
    // CHECK: obelisk_sim.logic.constant 0 : i64, -1 : i64
    // CHECK: obelisk_sim.logic.constant -1 : i64, -1 : i64
    // A leading known zero prevents the following X group from filling.
    // CHECK: obelisk_sim.logic.constant 0 : i64, 15 : i64
    // Signed high X uses ordinary signed four-state extension.
    // CHECK: obelisk_sim.logic.constant 0 : i64, -1 : i64
    // CHECK: obelisk_sim.return
    obelisk_sim.func @width(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "'shffffffff", is_declared_unsized = true,
            is_signed = true, node_id = 2 : i64,
            semantic_type = !signed_bit64} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 3 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "'sh80000000", is_declared_unsized = true,
            is_signed = true, node_id = 4 : i64,
            semantic_type = !signed_bit64} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 5 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "'sd4294967295", is_declared_unsized = true,
            is_signed = true, node_id = 6 : i64,
            semantic_type = !signed_bit64} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 7 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "'h1ffffffff", is_declared_unsized = true,
            node_id = 8 : i64, semantic_type = !bit32} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "'shfxxxxxxxx", is_declared_unsized = true,
            is_signed = true, node_id = 10 : i64,
            semantic_type = !signed_logic64} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "'shx00000000", is_declared_unsized = true,
            is_signed = true, node_id = 12 : i64,
            semantic_type = !signed_logic64} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "'hx", is_declared_unsized = true,
            node_id = 14 : i64, semantic_type = !logic64} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "'hz", is_declared_unsized = true,
            node_id = 16 : i64, semantic_type = !logic64} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 17 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "'h0x", is_declared_unsized = true,
            node_id = 18 : i64, semantic_type = !logic64} {
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 19 : i64} {
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "'shx", is_declared_unsized = true,
            is_signed = true, node_id = 20 : i64,
            semantic_type = !signed_logic64} {
        }
      }
      obelisk_sim.return
    }
  }
}
