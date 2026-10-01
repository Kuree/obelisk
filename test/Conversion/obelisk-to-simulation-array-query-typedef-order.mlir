// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 20.7 expands intermediate typedefs before numbering array
// dimensions. Storage selection still follows the flattened D,B,C,A nesting,
// but the first query dimension is the typedef's A range.
module {
  obelisk.sv.symbol.definition @top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @top} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64} {
        obelisk.sv.symbol.variable @x attributes {hierarchical_name = "top.x", lifetime = 1 : i32, name = "x", node_id = 5 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 6 x !obelisk.ranged_unpacked_array<0 : 4 x !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.ranged_unpacked_array<0 : 2 x !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>>>>} {
        }
        obelisk.sv.symbol.variable @result attributes {hierarchical_name = "top.result", lifetime = 1 : i32, name = "result", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 0 : i32, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
          obelisk.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false, node_id = 8 : i64} {
            // Slang folded this comparison using storage order. Preparation
            // must discard that stale zero after seeing the corrected query.
            obelisk.sv.expression.binary_op attributes {folded_constant = "1'b0", is_signed = false, node_id = 9 : i64, operator_kind = 11 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$size", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, folded_constant = "7", has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 10 : i64, obelisk.array_query_dimensions = [{kind = "fixed", left = 0 : i64, right = 2 : i64, unpacked = true}, {kind = "fixed", left = 0 : i64, right = 4 : i64, unpacked = true}, {kind = "fixed", left = 0 : i64, right = 1 : i64, unpacked = true}, {kind = "fixed", left = 0 : i64, right = 6 : i64, unpacked = true}, {kind = "fixed", left = 31 : i64, right = 0 : i64, unpacked = false}], semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @root::@instance::@body} {
                obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.ranged_unpacked_array<0 : 6 x !obelisk.ranged_unpacked_array<0 : 4 x !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.ranged_unpacked_array<0 : 2 x !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>>>>} {
                }
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "3", is_signed = true, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "top.result", referenced_symbol = @root::@instance::@body::@result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "42", is_signed = true, node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: arith.constant 42 : i32
// CHECK-NOT: arith.constant 7 : i32
