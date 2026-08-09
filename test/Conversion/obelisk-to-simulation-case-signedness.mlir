// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "case_signedness", name = "case_signedness", node_id = 0 : i64, sym_name = "s0.case_signedness"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "case_signedness", is_uninstantiated = false, name = "case_signedness", node_id = 3 : i64, referenced_path = "case_signedness", referenced_symbol = @s0.case_signedness, sym_name = "s3.case_signedness"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "case_signedness", name = "case_signedness", node_id = 4 : i64, sym_name = "s4.case_signedness"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "case_signedness.selector", lifetime = 1 : i32, name = "selector", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, sym_name = "s5.selector"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "case_signedness.matched", lifetime = 1 : i32, name = "matched", node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.matched"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "case_signedness", node_id = 6 : i64, procedure_kind = 0 : i32, sym_name = "s6", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.case attributes {check_kind = 0 : i32, condition_kind = 0 : i32, has_default = true, item_count = 1 : i64, item_label_counts = array<i64: 1>, node_id = 7 : i64} {
            obelisk.sv.expression.conversion attributes {node_id = 8 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "case_signedness.selector", referenced_symbol = @s1.$root::@s3.case_signedness::@s4.case_signedness::@s5.selector, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
              }
            }
            obelisk.sv.expression.conversion attributes {node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "4'b1111", node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 12 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 15 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "case_signedness.matched", referenced_symbol = @s1.$root::@s3.case_signedness::@s4.case_signedness::@s7.matched, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 17 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "case_signedness.matched", referenced_symbol = @s1.$root::@s3.case_signedness::@s4.case_signedness::@s7.matched, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", node_id = 20 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: %[[SELECTOR:.*]] = obelisk_sim.ref.load
// CHECK: %[[FLAT:.*]] = obelisk_sim.packed.flatten %[[SELECTOR]]
// CHECK: %[[EXTENDED:.*]] = obelisk_sim.logic.resize %[[FLAT]] signed = false : !obelisk_sim.logic<2> -> !obelisk_sim.logic<4>
// CHECK: obelisk_sim.logic.compare case_eq %[[EXTENDED]],
// CHECK-NOT: obelisk.sv.
