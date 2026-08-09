// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "expression_signedness", name = "expression_signedness", node_id = 0 : i64, sym_name = "s0.expression_signedness"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "expression_signedness", is_uninstantiated = false, name = "expression_signedness", node_id = 3 : i64, referenced_path = "expression_signedness", referenced_symbol = @s0.expression_signedness, sym_name = "s3.expression_signedness"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "expression_signedness", name = "expression_signedness", node_id = 4 : i64, sym_name = "s4.expression_signedness"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "expression_signedness.value", lifetime = 1 : i32, name = "value", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, sym_name = "s5.value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "expression_signedness.result", lifetime = 1 : i32, name = "result", node_id = 15 : i64, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, sym_name = "s15.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "expression_signedness", node_id = 6 : i64, procedure_kind = 0 : i32, sym_name = "s6", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 7 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 8 : i64, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "expression_signedness.result", referenced_symbol = @s1.$root::@s3.expression_signedness::@s4.expression_signedness::@s15.result, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
              }
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 10 : i64, operator_kind = 26 : i32, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "expression_signedness.value", referenced_symbol = @s1.$root::@s3.expression_signedness::@s4.expression_signedness::@s5.value, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_signed = true, node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 13 : i64, referenced_path = "expression_signedness.result", referenced_symbol = @s1.$root::@s3.expression_signedness::@s4.expression_signedness::@s15.result, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
              }
              obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 14 : i64, operator_kind = 26 : i32, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 18 : i64, referenced_path = "expression_signedness.value", referenced_symbol = @s1.$root::@s3.expression_signedness::@s4.expression_signedness::@s5.value, semantic_type = !obelisk.ranged_packed_array<11 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_signed = true, node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: obelisk_sim.logic.shift right %
// CHECK: obelisk_sim.logic.shift right_arith
// CHECK-NOT: obelisk.sv.
