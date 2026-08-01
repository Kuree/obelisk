// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// Each $timeformat argument has its own default, so a call with none restores
// the IEEE defaults rather than lowering to nothing.

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-DAG: %[[SUFFIX:.*]] = obelisk_sim.bytes.constant "ns"
// CHECK-DAG: %[[EMPTY:.*]] = obelisk_sim.bytes.constant ""
// CHECK-DAG: %[[UNITS:.*]] = arith.constant -9 : i32
// CHECK-DAG: %[[DIGITS:.*]] = arith.constant 5 : i32
// CHECK-DAG: %[[WIDTH:.*]] = arith.constant 10 : i32
// CHECK-DAG: %[[ZERO:.*]] = arith.constant 0 : i32
// CHECK-DAG: %[[DEFAULT_WIDTH:.*]] = arith.constant 20 : i32
// CHECK: obelisk_sim.time.format {{.*}}, %[[UNITS]], %[[DIGITS]], %[[SUFFIX]], %[[WIDTH]]
// CHECK: obelisk_sim.time.format {{.*}}, %[[UNITS]], %[[ZERO]], %[[EMPTY]], %[[DEFAULT_WIDTH]]

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, original_source_range = !obelisk.source_range<"timeformat.sv", 1, 1, "timeformat.sv", 6, 10, "">, source_end_column = 10 : i64, source_end_line = 6 : i64, source_file = "timeformat.sv", source_range = !obelisk.source_range<"timeformat.sv", 1, 1, "timeformat.sv", 6, 10, "">, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, original_source_range = !obelisk.source_range<"timeformat.sv", 1, 1, "timeformat.sv", 7, 1, "">, source_end_column = 1 : i64, source_end_line = 7 : i64, source_file = "timeformat.sv", source_range = !obelisk.source_range<"timeformat.sv", 1, 1, "timeformat.sv", 7, 1, "">, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, original_source_range = !obelisk.source_range<"timeformat.sv", 1, 8, "timeformat.sv", 1, 8, "">, referenced_path = "top", referenced_symbol = @s0.top, source_end_column = 8 : i64, source_end_line = 1 : i64, source_file = "timeformat.sv", source_range = !obelisk.source_range<"timeformat.sv", 1, 8, "timeformat.sv", 1, 8, "">, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, original_source_range = !obelisk.source_range<"timeformat.sv", 1, 1, "timeformat.sv", 6, 10, "">, source_end_column = 10 : i64, source_end_line = 6 : i64, source_file = "timeformat.sv", source_range = !obelisk.source_range<"timeformat.sv", 1, 1, "timeformat.sv", 6, 10, "">, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 5 : i64, original_source_range = !obelisk.source_range<"timeformat.sv", 2, 1, "timeformat.sv", 5, 4, "">, procedure_kind = 0 : i32, source_end_column = 4 : i64, source_end_line = 5 : i64, source_file = "timeformat.sv", source_range = !obelisk.source_range<"timeformat.sv", 2, 1, "timeformat.sv", 5, 4, "">, sym_name = "s5", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 6 : i64, original_source_range = !obelisk.source_range<"timeformat.sv", 2, 9, "timeformat.sv", 5, 4, "">, source_end_column = 4 : i64, source_end_line = 5 : i64, source_file = "timeformat.sv", source_range = !obelisk.source_range<"timeformat.sv", 2, 9, "timeformat.sv", 5, 4, "">} {
            obelisk.sv.statement.list attributes {node_id = 7 : i64, original_source_range = !obelisk.source_range<"timeformat.sv", 2, 9, "timeformat.sv", 5, 4, "">, source_end_column = 4 : i64, source_end_line = 5 : i64, source_file = "timeformat.sv", source_range = !obelisk.source_range<"timeformat.sv", 2, 9, "timeformat.sv", 5, 4, "">} {
              obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64, original_source_range = !obelisk.source_range<"timeformat.sv", 3, 2, "timeformat.sv", 3, 31, "">, source_end_column = 31 : i64, source_end_line = 3 : i64, source_file = "timeformat.sv", source_range = !obelisk.source_range<"timeformat.sv", 3, 2, "timeformat.sv", 3, 31, "">} {
                obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$timeformat", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 9 : i64, original_source_range = !obelisk.source_range<"timeformat.sv", 3, 2, "timeformat.sv", 3, 30, "">, semantic_type = !obelisk.void, source_end_column = 30 : i64, source_end_line = 3 : i64, source_file = "timeformat.sv", source_range = !obelisk.source_range<"timeformat.sv", 3, 2, "timeformat.sv", 3, 30, "">, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  obelisk.sv.expression.unary_op attributes {node_id = 10 : i64, operator_kind = 1 : i32, original_source_range = !obelisk.source_range<"timeformat.sv", 3, 14, "timeformat.sv", 3, 16, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 16 : i64, source_end_line = 3 : i64, source_file = "timeformat.sv", source_range = !obelisk.source_range<"timeformat.sv", 3, 14, "timeformat.sv", 3, 16, "">} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "9", node_id = 11 : i64, original_source_range = !obelisk.source_range<"timeformat.sv", 3, 15, "timeformat.sv", 3, 16, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 16 : i64, source_end_line = 3 : i64, source_file = "timeformat.sv", source_range = !obelisk.source_range<"timeformat.sv", 3, 15, "timeformat.sv", 3, 16, "">} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "5", node_id = 12 : i64, original_source_range = !obelisk.source_range<"timeformat.sv", 3, 18, "timeformat.sv", 3, 19, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 19 : i64, source_end_line = 3 : i64, source_file = "timeformat.sv", source_range = !obelisk.source_range<"timeformat.sv", 3, 18, "timeformat.sv", 3, 19, "">} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 13 : i64, original_source_range = !obelisk.source_range<"timeformat.sv", 3, 21, "timeformat.sv", 3, 25, "">, semantic_type = !obelisk.string, source_end_column = 25 : i64, source_end_line = 3 : i64, source_file = "timeformat.sv", source_range = !obelisk.source_range<"timeformat.sv", 3, 21, "timeformat.sv", 3, 25, "">} {
                    obelisk.sv.expression.string_literal attributes {constant_value = "ns", node_id = 14 : i64, original_source_range = !obelisk.source_range<"timeformat.sv", 3, 21, "timeformat.sv", 3, 25, "">, semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, source_end_column = 25 : i64, source_end_line = 3 : i64, source_file = "timeformat.sv", source_range = !obelisk.source_range<"timeformat.sv", 3, 21, "timeformat.sv", 3, 25, "">} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "10", node_id = 15 : i64, original_source_range = !obelisk.source_range<"timeformat.sv", 3, 27, "timeformat.sv", 3, 29, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 29 : i64, source_end_line = 3 : i64, source_file = "timeformat.sv", source_range = !obelisk.source_range<"timeformat.sv", 3, 27, "timeformat.sv", 3, 29, "">} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 16 : i64, original_source_range = !obelisk.source_range<"timeformat.sv", 4, 2, "timeformat.sv", 4, 16, "">, source_end_column = 16 : i64, source_end_line = 4 : i64, source_file = "timeformat.sv", source_range = !obelisk.source_range<"timeformat.sv", 4, 2, "timeformat.sv", 4, 16, "">} {
                obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$timeformat", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 17 : i64, original_source_range = !obelisk.source_range<"timeformat.sv", 4, 2, "timeformat.sv", 4, 15, "">, semantic_type = !obelisk.void, source_end_column = 15 : i64, source_end_line = 4 : i64, source_file = "timeformat.sv", source_range = !obelisk.source_range<"timeformat.sv", 4, 2, "timeformat.sv", 4, 15, "">, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                }
              }
            }
          }
        }
      }
    }
  }
}

