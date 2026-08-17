// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// The format is split at compile time: each conversion becomes one scan-field
// op carrying the literal text before it, and the field it returns is parsed
// with the same primitives the string conversion methods use. The running
// success flag guards each store and gates the cursor, so a failed conversion
// ends the scan without a branch.

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK: %[[FIELD0:.*]], %[[CURSOR0:.*]], %[[OK0:.*]] = obelisk_sim.string.scan_field {{.*}} {prefix = "", specifier = 100 : i32}
// CHECK: obelisk_sim.string.parse_logic %[[FIELD0]] radix = 10 : <64>
// CHECK: arith.cmpi ne, %[[OK0]]
// CHECK: arith.select
// CHECK: %[[FIELD1:.*]], %[[CURSOR1:.*]], %[[OK1:.*]] = obelisk_sim.string.scan_field {{.*}} {prefix = " ", specifier = 102 : i32}
// CHECK: obelisk_sim.string.parse_real %[[FIELD1]]
// CHECK: %[[FIELD2:.*]], %[[CURSOR2:.*]], %[[OK2:.*]] = obelisk_sim.string.scan_field {{.*}} {prefix = " ", specifier = 115 : i32}
// CHECK: %[[MATCHED2:.*]] = arith.cmpi ne, %[[OK2]]
// CHECK: %[[LIVE2:.*]] = arith.andi {{.*}}, %[[MATCHED2]]
// CHECK: %[[TEXT:.*]] = arith.select %[[LIVE2]], %[[FIELD2]]
// CHECK: obelisk_sim.ref.store %[[TEXT]]

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, original_source_range = !obelisk.source_range<"scan.sv", 1, 1, "scan.sv", 9, 10, "">, source_end_column = 10 : i64, source_end_line = 9 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 1, 1, "scan.sv", 9, 10, "">, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, original_source_range = !obelisk.source_range<"scan.sv", 1, 1, "scan.sv", 10, 1, "">, source_end_column = 1 : i64, source_end_line = 10 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 1, 1, "scan.sv", 10, 1, "">, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, original_source_range = !obelisk.source_range<"scan.sv", 1, 8, "scan.sv", 1, 8, "">, referenced_path = "top", referenced_symbol = @s0.top, source_end_column = 8 : i64, source_end_line = 1 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 1, 8, "scan.sv", 1, 8, "">, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, original_source_range = !obelisk.source_range<"scan.sv", 1, 1, "scan.sv", 9, 10, "">, source_end_column = 10 : i64, source_end_line = 9 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 1, 1, "scan.sv", 9, 10, "">, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.count", lifetime = 1 : i32, name = "count", node_id = 5 : i64, original_source_range = !obelisk.source_range<"scan.sv", 2, 9, "scan.sv", 2, 14, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 14 : i64, source_end_line = 2 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 2, 9, "scan.sv", 2, 14, "">, sym_name = "s5.count"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.first", lifetime = 1 : i32, name = "first", node_id = 6 : i64, original_source_range = !obelisk.source_range<"scan.sv", 3, 9, "scan.sv", 3, 14, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 14 : i64, source_end_line = 3 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 3, 9, "scan.sv", 3, 14, "">, sym_name = "s6.first"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.second", lifetime = 1 : i32, name = "second", node_id = 7 : i64, original_source_range = !obelisk.source_range<"scan.sv", 4, 6, "scan.sv", 4, 12, "">, semantic_type = !obelisk.real, source_end_column = 12 : i64, source_end_line = 4 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 4, 6, "scan.sv", 4, 12, "">, sym_name = "s7.second"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.word", lifetime = 1 : i32, name = "word", node_id = 8 : i64, original_source_range = !obelisk.source_range<"scan.sv", 5, 8, "scan.sv", 5, 12, "">, semantic_type = !obelisk.string, source_end_column = 12 : i64, source_end_line = 5 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 5, 8, "scan.sv", 5, 12, "">, sym_name = "s8.word"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 9 : i64, original_source_range = !obelisk.source_range<"scan.sv", 6, 1, "scan.sv", 8, 4, "">, procedure_kind = 0 : i32, source_end_column = 4 : i64, source_end_line = 8 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 6, 1, "scan.sv", 8, 4, "">, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 10 : i64, original_source_range = !obelisk.source_range<"scan.sv", 7, 2, "scan.sv", 7, 63, "">, source_end_column = 63 : i64, source_end_line = 7 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 7, 2, "scan.sv", 7, 63, "">} {
            obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64, original_source_range = !obelisk.source_range<"scan.sv", 7, 2, "scan.sv", 7, 63, "">, source_end_column = 63 : i64, source_end_line = 7 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 7, 2, "scan.sv", 7, 63, "">} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 12 : i64, original_source_range = !obelisk.source_range<"scan.sv", 7, 2, "scan.sv", 7, 62, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 62 : i64, source_end_line = 7 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 7, 2, "scan.sv", 7, 62, "">} {
                obelisk.sv.expression.named_value attributes {node_id = 13 : i64, original_source_range = !obelisk.source_range<"scan.sv", 7, 2, "scan.sv", 7, 7, "">, referenced_path = "top.count", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.count, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 7 : i64, source_end_line = 7 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 7, 2, "scan.sv", 7, 7, "">} {
                }
                obelisk.sv.expression.call attributes {argument_count = 5 : i64, callee_name = "$sscanf", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 14 : i64, original_source_range = !obelisk.source_range<"scan.sv", 7, 10, "scan.sv", 7, 62, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 62 : i64, source_end_line = 7 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 7, 10, "scan.sv", 7, 62, "">, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "1 2.5 hi", node_id = 15 : i64, original_source_range = !obelisk.source_range<"scan.sv", 7, 18, "scan.sv", 7, 28, "">, semantic_type = !obelisk.ranged_packed_array<63 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, source_end_column = 28 : i64, source_end_line = 7 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 7, 18, "scan.sv", 7, 28, "">} {
                  }
                  obelisk.sv.expression.string_literal attributes {constant_value = "%d %f %s", node_id = 16 : i64, original_source_range = !obelisk.source_range<"scan.sv", 7, 30, "scan.sv", 7, 40, "">, semantic_type = !obelisk.ranged_packed_array<63 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, source_end_column = 40 : i64, source_end_line = 7 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 7, 30, "scan.sv", 7, 40, "">} {
                  }
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 17 : i64, original_source_range = !obelisk.source_range<"scan.sv", 7, 42, "scan.sv", 7, 47, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 47 : i64, source_end_line = 7 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 7, 42, "scan.sv", 7, 47, "">} {
                    obelisk.sv.expression.named_value attributes {node_id = 18 : i64, original_source_range = !obelisk.source_range<"scan.sv", 7, 42, "scan.sv", 7, 47, "">, referenced_path = "top.first", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.first, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 47 : i64, source_end_line = 7 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 7, 42, "scan.sv", 7, 47, "">} {
                    }
                    obelisk.sv.expression.empty_argument attributes {node_id = 19 : i64, original_source_range = !obelisk.source_range<"scan.sv", 7, 42, "scan.sv", 7, 47, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 47 : i64, source_end_line = 7 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 7, 42, "scan.sv", 7, 47, "">} {
                    }
                  }
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 20 : i64, original_source_range = !obelisk.source_range<"scan.sv", 7, 49, "scan.sv", 7, 55, "">, semantic_type = !obelisk.real, source_end_column = 55 : i64, source_end_line = 7 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 7, 49, "scan.sv", 7, 55, "">} {
                    obelisk.sv.expression.named_value attributes {node_id = 21 : i64, original_source_range = !obelisk.source_range<"scan.sv", 7, 49, "scan.sv", 7, 55, "">, referenced_path = "top.second", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.second, semantic_type = !obelisk.real, source_end_column = 55 : i64, source_end_line = 7 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 7, 49, "scan.sv", 7, 55, "">} {
                    }
                    obelisk.sv.expression.empty_argument attributes {node_id = 22 : i64, original_source_range = !obelisk.source_range<"scan.sv", 7, 49, "scan.sv", 7, 55, "">, semantic_type = !obelisk.real, source_end_column = 55 : i64, source_end_line = 7 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 7, 49, "scan.sv", 7, 55, "">} {
                    }
                  }
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 23 : i64, original_source_range = !obelisk.source_range<"scan.sv", 7, 57, "scan.sv", 7, 61, "">, semantic_type = !obelisk.string, source_end_column = 61 : i64, source_end_line = 7 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 7, 57, "scan.sv", 7, 61, "">} {
                    obelisk.sv.expression.named_value attributes {node_id = 24 : i64, original_source_range = !obelisk.source_range<"scan.sv", 7, 57, "scan.sv", 7, 61, "">, referenced_path = "top.word", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.word, semantic_type = !obelisk.string, source_end_column = 61 : i64, source_end_line = 7 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 7, 57, "scan.sv", 7, 61, "">} {
                    }
                    obelisk.sv.expression.empty_argument attributes {node_id = 25 : i64, original_source_range = !obelisk.source_range<"scan.sv", 7, 57, "scan.sv", 7, 61, "">, semantic_type = !obelisk.string, source_end_column = 61 : i64, source_end_line = 7 : i64, source_file = "scan.sv", source_range = !obelisk.source_range<"scan.sv", 7, 57, "scan.sv", 7, 61, "">} {
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
