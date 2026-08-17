// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 11.8.2: a context-determined operand is first coerced to the
// type of the expression and only then extended, so the signed arm of a
// conditional whose result is unsigned must be zero-extended.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "conditional_signedness", name = "conditional_signedness", node_id = 0 : i64, sym_name = "s0.conditional_signedness"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "conditional_signedness", is_uninstantiated = false, name = "conditional_signedness", node_id = 3 : i64, referenced_path = "conditional_signedness", referenced_symbol = @s0.conditional_signedness, sym_name = "s3.conditional_signedness"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "conditional_signedness", name = "conditional_signedness", node_id = 4 : i64, sym_name = "s4.conditional_signedness"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "conditional_signedness.arm", lifetime = 1 : i32, name = "arm", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, sym_name = "s5.arm"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "conditional_signedness.other", lifetime = 1 : i32, name = "other", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s6.other"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "conditional_signedness.result", lifetime = 1 : i32, name = "result", node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s7.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "conditional_signedness", node_id = 8 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "conditional_signedness.result", referenced_symbol = @s1.$root::@s3.conditional_signedness::@s4.conditional_signedness::@s7.result, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
              obelisk.sv.expression.conditional_op attributes {condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "conditional_signedness.other", referenced_symbol = @s1.$root::@s3.conditional_signedness::@s4.conditional_signedness::@s6.other, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
                obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 15 : i64, referenced_path = "conditional_signedness.arm", referenced_symbol = @s1.$root::@s3.conditional_signedness::@s4.conditional_signedness::@s5.arm, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                  }
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "conditional_signedness.other", referenced_symbol = @s1.$root::@s3.conditional_signedness::@s4.conditional_signedness::@s6.other, semantic_type = !obelisk.ranged_packed_array<2 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-NOT: obelisk_sim.logic.resize {{.*}} signed = true : !obelisk_sim.logic<2> -> !obelisk_sim.logic<3>
// CHECK: obelisk_sim.logic.resize {{.*}} signed = false : !obelisk_sim.logic<2> -> !obelisk_sim.logic<3>
// CHECK-NOT: obelisk.sv.
