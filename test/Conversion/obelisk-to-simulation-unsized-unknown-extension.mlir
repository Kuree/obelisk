// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 5.7.1: "Unsized unsigned literal constants where the
// high-order bit is unknown (X or x) or three-state (Z or z) shall be extended
// to the size of the expression containing the literal constant." Slang widens
// the unsized `'dX` here with the zero padding of an ordinary conversion, so
// its cached comparison result says the two all-unknown operands differ.
// Preparation must discard that stale fold and let the comparison lower, which
// fills the unknown bit through all 68 bits.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "unit"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @top, sym_name = "instance"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "body"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.result", lifetime = 1 : i32, name = "result", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 6 : i64, procedure_kind = 0 : i32, sym_name = "initial", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
          obelisk.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false, node_id = 7 : i64} {
            obelisk.sv.expression.binary_op attributes {folded_constant = "1'b1", is_signed = false, node_id = 8 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "68'bxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", is_signed = false, node_id = 9 : i64, semantic_type = !obelisk.ranged_packed_array<67 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
              obelisk.sv.expression.conversion attributes {folded_constant = "68'b0xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", is_implicit = true, is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<67 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "32'bxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", is_declared_unsized = true, is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 12 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 14 : i64, referenced_path = "top.result", referenced_symbol = @root::@instance::@body::@result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "42", is_declared_unsized = true, is_signed = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// Both operands then carry the same 68 unknown bits, so the case inequality is
// false and the guarded assignment never runs.
// CHECK: simulation.func private @unit_0
// CHECK-NEXT: simulation.return
// CHECK-NOT: arith.constant 42 : i32
