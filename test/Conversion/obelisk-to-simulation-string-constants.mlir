// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 5.9 makes a string literal's value exactly the characters it
// encloses, so a parameter's frozen spelling is taken as written: the padding
// around `"  a  "` is part of the value.
//
// 6.18 and 10.9.1 also allow a parameter to be an unpacked array of strings.
// Its elaborated spelling quotes each element, so a comma inside one is not an
// element separator -- `["x,y","z"]` is the two-element array the source
// wrote, not a three-element one.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "t", name = "t", node_id = 0 : i64, sym_name = "s0.t"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "t", is_uninstantiated = false, name = "t", node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t, sym_name = "s3.t"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "t", name = "t", node_id = 4 : i64, sym_name = "s4.t", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.parameter attributes {constant_value = "  a  ", hierarchical_name = "t.PAD", name = "PAD", node_id = 5 : i64, semantic_type = !obelisk.string, sym_name = "s5.PAD"} {
          obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 6 : i64, semantic_type = !obelisk.string} {
            obelisk.sv.expression.string_literal attributes {constant_value = "  a  ", folded_constant = "40'h2020612020", is_signed = false, node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<39 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
            }
          }
        }
        obelisk.sv.symbol.parameter attributes {constant_value = "[\22x,y\22,\22z\22]", hierarchical_name = "t.REGS", name = "REGS", node_id = 8 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.string>, sym_name = "s6.REGS"} {
          obelisk.sv.expression.simple_assignment_pattern attributes {is_signed = false, node_id = 9 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.string>} {
            obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.string} {
              obelisk.sv.expression.string_literal attributes {constant_value = "x,y", folded_constant = "24'd7875705", is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<23 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
            }
            obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.string} {
              obelisk.sv.expression.string_literal attributes {constant_value = "z", folded_constant = "8'd122", is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "t.s", lifetime = 1 : i32, name = "s", node_id = 14 : i64, semantic_type = !obelisk.string, sym_name = "s7.s"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "t", node_id = 15 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 16 : i64} {
            obelisk.sv.statement.list attributes {node_id = 17 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 19 : i64, semantic_type = !obelisk.string} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 20 : i64, referenced_path = "t.s", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s7.s, semantic_type = !obelisk.string} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "t.PAD", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.PAD, semantic_type = !obelisk.string} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 22 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 23 : i64, semantic_type = !obelisk.string} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "t.s", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s7.s, semantic_type = !obelisk.string} {
                  }
                  obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 25 : i64, semantic_type = !obelisk.string} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 26 : i64, referenced_path = "t.REGS", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.REGS, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.string>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
// CHECK: obelisk_sim.string.literal "  a  "
// CHECK: obelisk_sim.string.literal "x,y"
// CHECK: obelisk_sim.string.literal "z"
// Splitting on the comma would leave `"x` and `y"` and one element too many.
// CHECK-NOT: obelisk_sim.string.literal "y
