// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 6.16: "A string variable shall not contain the special
// character "\0"." The zero byte is dropped when the literal is converted to
// the string type, which is also what the packed-to-string runtime conversion
// does.
//
// The same literal assigned to a packed variable keeps its zero byte, because
// 5.9 makes string literals behave like packed arrays there. Both assignments
// below come from the identical literal, so they pin the two halves apart.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "string_literal_nul", name = "string_literal_nul", node_id = 0 : i64, sym_name = "s0.string_literal_nul"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "string_literal_nul", is_uninstantiated = false, name = "string_literal_nul", node_id = 3 : i64, referenced_path = "string_literal_nul", referenced_symbol = @s0.string_literal_nul, sym_name = "s3.string_literal_nul"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "string_literal_nul", name = "string_literal_nul", node_id = 4 : i64, sym_name = "s4.string_literal_nul", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "string_literal_nul.text", lifetime = 1 : i32, name = "text", node_id = 5 : i64, semantic_type = !obelisk.string, sym_name = "s5.text"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "string_literal_nul.packed_bits", lifetime = 1 : i32, name = "packed_bits", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<23 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s6.packed_bits"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "string_literal_nul", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.string} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "string_literal_nul.text", referenced_symbol = @s1.$root::@s3.string_literal_nul::@s4.string_literal_nul::@s5.text, semantic_type = !obelisk.string} {
                  }
                  obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.string} {
                    obelisk.sv.expression.string_literal attributes {constant_value = "A\00B", is_signed = false, node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<23 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 16 : i64, semantic_type = !obelisk.ranged_packed_array<23 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "string_literal_nul.packed_bits", referenced_symbol = @s1.$root::@s3.string_literal_nul::@s4.string_literal_nul::@s6.packed_bits, semantic_type = !obelisk.ranged_packed_array<23 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.conversion attributes {folded_constant = "24'd4259906", is_signed = false, node_id = 18 : i64, semantic_type = !obelisk.ranged_packed_array<23 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.string_literal attributes {constant_value = "A\00B", folded_constant = "24'd4259906", is_signed = false, node_id = 19 : i64, semantic_type = !obelisk.ranged_packed_array<23 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}


// CHECK-LABEL: obelisk_sim.func private @unit_0

// 4259906 is 0x410042: the packed value keeps the zero byte.
// CHECK: obelisk_sim.logic.constant 4259906 : i24, 0 : i24

// The string variable does not.
// CHECK: obelisk_sim.string.literal "AB"
// CHECK-NOT: obelisk_sim.string.literal "A\00B"
