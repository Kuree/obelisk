// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' -o /dev/null 2>&1 | FileCheck %s --check-prefix=WARN

// IEEE 1800-2017 23.3.3.6 requires one warning per port connection when a
// uwire occurs on either side but the endpoints are lowered without net
// collapsing. The input covers an internal uwire with a variable actual; the
// output covers an external uwire with a variable formal. A third input uses
// that uwire only as a binary operand and must not produce another warning.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "uwire_nonmerged", name = "uwire_nonmerged", node_id = 0 : i64, sym_name = "s0.uwire_nonmerged"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "uwire_nonmerged_child", name = "uwire_nonmerged_child", node_id = 1 : i64, sym_name = "s1.uwire_nonmerged_child"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "s2.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "uwire_nonmerged", is_uninstantiated = false, name = "uwire_nonmerged", node_id = 4 : i64, referenced_path = "uwire_nonmerged", referenced_symbol = @s0.uwire_nonmerged, sym_name = "s4.uwire_nonmerged"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "uwire_nonmerged", name = "uwire_nonmerged", node_id = 5 : i64, sym_name = "s5.uwire_nonmerged"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "uwire_nonmerged.source", lifetime = 1 : i32, name = "source", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.source"} {
        }
        obelisk.sv.symbol.net attributes {hierarchical_name = "uwire_nonmerged.destination", is_implicit = false, name = "destination", net_kind = 12 : i32, node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.destination"} {
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "uwire_nonmerged.child", is_uninstantiated = false, name = "child", node_id = 8 : i64, referenced_path = "uwire_nonmerged_child", referenced_symbol = @s1.uwire_nonmerged_child, sym_name = "s8.child"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "internal_uwire", formal_ordinal = 0 : i64, formal_path = "uwire_nonmerged.child.internal_uwire", formal_symbol = @s2.$root::@s4.uwire_nonmerged::@s5.uwire_nonmerged::@s8.child::@s9.uwire_nonmerged_child::@s10.internal_uwire, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "uwire_nonmerged.child.internal_uwire", internal_symbol = @s2.$root::@s4.uwire_nonmerged::@s5.uwire_nonmerged::@s8.child::@s9.uwire_nonmerged_child::@s11.internal_uwire, is_ansi = true, is_net = true, node_id = 9 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "uwire_nonmerged.source", referenced_symbol = @s2.$root::@s4.uwire_nonmerged::@s5.uwire_nonmerged::@s6.source, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "internal_variable", formal_ordinal = 1 : i64, formal_path = "uwire_nonmerged.child.internal_variable", formal_symbol = @s2.$root::@s4.uwire_nonmerged::@s5.uwire_nonmerged::@s8.child::@s9.uwire_nonmerged_child::@s12.internal_variable, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "uwire_nonmerged.child.internal_variable", internal_symbol = @s2.$root::@s4.uwire_nonmerged::@s5.uwire_nonmerged::@s8.child::@s9.uwire_nonmerged_child::@s13.internal_variable, is_ansi = true, is_net = false, node_id = 11 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "uwire_nonmerged.destination", referenced_symbol = @s2.$root::@s4.uwire_nonmerged::@s5.uwire_nonmerged::@s7.destination, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.empty_argument attributes {node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "compound_variable", formal_ordinal = 2 : i64, formal_path = "uwire_nonmerged.child.compound_variable", formal_symbol = @s2.$root::@s4.uwire_nonmerged::@s5.uwire_nonmerged::@s8.child::@s9.uwire_nonmerged_child::@s14.compound_variable, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "uwire_nonmerged.child.compound_variable", internal_symbol = @s2.$root::@s4.uwire_nonmerged::@s5.uwire_nonmerged::@s8.child::@s9.uwire_nonmerged_child::@s15.compound_variable, is_ansi = true, is_net = false, node_id = 20 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.binary_op attributes {node_id = 21 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "uwire_nonmerged.destination", referenced_symbol = @s2.$root::@s4.uwire_nonmerged::@s5.uwire_nonmerged::@s7.destination, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 23 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "uwire_nonmerged.child", name = "uwire_nonmerged_child", node_id = 15 : i64, sym_name = "s9.uwire_nonmerged_child"} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "uwire_nonmerged.child.internal_uwire", name = "internal_uwire", node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s10.internal_uwire"} {
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "uwire_nonmerged.child.internal_uwire", is_implicit = false, name = "internal_uwire", net_kind = 12 : i32, node_id = 17 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s11.internal_uwire"} {
            }
            obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "uwire_nonmerged.child.internal_variable", name = "internal_variable", node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s12.internal_variable"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "uwire_nonmerged.child.internal_variable", lifetime = 1 : i32, name = "internal_variable", node_id = 19 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s13.internal_variable"} {
            }
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "uwire_nonmerged.child.compound_variable", name = "compound_variable", node_id = 24 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s14.compound_variable"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "uwire_nonmerged.child.compound_variable", lifetime = 1 : i32, name = "compound_variable", node_id = 25 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s15.compound_variable"} {
            }
          }
        }
      }
    }
  }
}

// WARN-COUNT-2: warning: uwire port connection was not fully merged into a single simulated net
// WARN-NOT: warning: uwire port connection was not fully merged into a single simulated net
