// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' --emit-bytecode -o /dev/null

!bit = !obelisk.integral<1, false, true, 0 : 0, logic>
!word = !obelisk.ranged_packed_array<0 : 0 x !bit>
!outside = !obelisk.ranged_packed_array<0 : 1 x !bit>

module {
  obelisk.sv.symbol.definition @definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 2 : i64, referenced_path = "top", referenced_symbol = @definition} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", node_id = 3 : i64} {
        obelisk.sv.symbol.variable @value attributes {hierarchical_name = "top.value", lifetime = 1 : i32, name = "value", node_id = 4 : i64, semantic_type = !word} {
        }
        obelisk.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", node_id = 5 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false, node_id = 6 : i64} {
            obelisk.sv.expression.binary_op attributes {folded_constant = "1'b0", is_signed = false, node_id = 7 : i64, operator_kind = 14 : i32, semantic_type = !bit} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", is_signed = false, node_id = 8 : i64, semantic_type = !bit} {
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 9 : i64, semantic_type = !bit} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, is_signed = false, node_id = 11 : i64, semantic_type = !outside} {
                obelisk.sv.expression.range_select attributes {is_signed = false, node_id = 12 : i64, selection_kind = 0 : i32, semantic_type = !outside} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "top.value", referenced_symbol = @root::@instance::@body::@value, semantic_type = !word} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_signed = true, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_signed = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "2'b00", is_signed = false, node_id = 16 : i64, semantic_type = !outside} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: simulation.func
// CHECK-NOT: simulation.nba
