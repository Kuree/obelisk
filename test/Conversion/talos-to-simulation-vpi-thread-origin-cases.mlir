// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// Every case label group and the default own a distinct CaseItem.
module {
  talos.sv.symbol.definition @s0.case_signedness attributes {definition_kind = 0 : i32, hierarchical_name = "case_signedness", name = "case_signedness", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    talos.sv.symbol.instance @s3.case_signedness attributes {hierarchical_name = "case_signedness", is_uninstantiated = false, name = "case_signedness", node_id = 3 : i64, referenced_path = "case_signedness", referenced_symbol = @s0.case_signedness} {
      talos.sv.symbol.instance_body @s4.case_signedness attributes {hierarchical_name = "case_signedness", name = "case_signedness", node_id = 4 : i64} {
        talos.sv.symbol.variable @s5.selector attributes {hierarchical_name = "case_signedness.selector", lifetime = 1 : i32, name = "selector", node_id = 5 : i64, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, true, true, 0 : 0, logic>>} {
        }
        talos.sv.symbol.variable @s7.matched attributes {hierarchical_name = "case_signedness.matched", lifetime = 1 : i32, name = "matched", node_id = 14 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
        talos.sv.symbol.procedural_block @s6 attributes {hierarchical_name = "case_signedness", node_id = 6 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.case attributes {check_kind = 0 : i32, condition_kind = 0 : i32, has_default = true, item_count = 1 : i64, item_label_counts = array<i64: 1>, node_id = 7 : i64} {
            talos.sv.expression.conversion attributes {node_id = 8 : i64, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
              talos.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "case_signedness.selector", referenced_symbol = @s1.$root::@s3.case_signedness::@s4.case_signedness::@s5.selector, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, true, true, 0 : 0, logic>>} {
              }
            }
            talos.sv.expression.conversion attributes {node_id = 10 : i64, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
              talos.sv.expression.integer_literal attributes {constant_value = "4'b1111", node_id = 11 : i64, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
              }
            }
            talos.sv.statement.expression_statement attributes {node_id = 12 : i64} {
              talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 15 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                talos.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "case_signedness.matched", referenced_symbol = @s1.$root::@s3.case_signedness::@s4.case_signedness::@s7.matched, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                }
                talos.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 17 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
            talos.sv.statement.expression_statement attributes {node_id = 13 : i64} {
              talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 18 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                talos.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "case_signedness.matched", referenced_symbol = @s1.$root::@s3.case_signedness::@s4.case_signedness::@s7.matched, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                }
                talos.sv.expression.integer_literal attributes {constant_value = "1'b0", node_id = 20 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: simulation.code_unit.decl [[UNIT:[0-9]+]] in [[SCOPE:[0-9]+]] initial
// CHECK: simulation.statement.decl [[CASE:[0-9]+]] in [[UNIT]] scope [[SCOPE]] type 5
// CHECK: simulation.vpi_thread_origin_binding [[UNIT]] origin [[CASE]]
// CHECK: simulation.statement.decl [[ITEM:[0-9]+]] in [[UNIT]] scope [[SCOPE]] type 6 parent [[CASE]]
// CHECK: simulation.vpi_statement_relation.decl statement [[CASE]] type 5 selector 6 ordinal 0 modes 2 to [[ITEM]]
// CHECK: simulation.statement.decl [[ASSIGN:[0-9]+]] in [[UNIT]] scope [[SCOPE]] type 3 parent [[ITEM]]
// CHECK: simulation.vpi_statement_relation.decl statement [[ITEM]] type 6 selector 104 ordinal 0 modes 1 to [[ASSIGN]]
// CHECK: simulation.statement.decl [[DEFAULT:[0-9]+]] in [[UNIT]] scope [[SCOPE]] type 6 parent [[CASE]]
// CHECK: simulation.vpi_statement_relation.decl statement [[CASE]] type 5 selector 6 ordinal 1 modes 2 to [[DEFAULT]]
// CHECK: simulation.statement.decl [[OTHER:[0-9]+]] in [[UNIT]] scope [[SCOPE]] type 3 parent [[DEFAULT]]
