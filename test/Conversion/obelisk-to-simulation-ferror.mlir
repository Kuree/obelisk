// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// $ferror yields the descriptor's pending error code and stores the host
// message into the string destination.

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK: %[[FD:.*]] = obelisk_sim.file.open
// CHECK: %[[MESSAGE:.*]], %[[CODE:.*]] = obelisk_sim.file.error_string
// CHECK: obelisk_sim.ref.store %[[MESSAGE]]
// CHECK: obelisk_sim.logic.from_bits %[[CODE]]

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 1, 1, "ferror.sv", 10, 10, "">, source_end_column = 10 : i64, source_end_line = 10 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 1, 1, "ferror.sv", 10, 10, "">, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 1, 1, "ferror.sv", 11, 1, "">, source_end_column = 1 : i64, source_end_line = 11 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 1, 1, "ferror.sv", 11, 1, "">, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 1, 8, "ferror.sv", 1, 8, "">, referenced_path = "top", referenced_symbol = @s0.top, source_end_column = 8 : i64, source_end_line = 1 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 1, 8, "ferror.sv", 1, 8, "">, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 1, 1, "ferror.sv", 10, 10, "">, source_end_column = 10 : i64, source_end_line = 10 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 1, 1, "ferror.sv", 10, 10, "">, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.fd", lifetime = 1 : i32, name = "fd", node_id = 5 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 2, 5, "ferror.sv", 2, 7, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 7 : i64, source_end_line = 2 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 2, 5, "ferror.sv", 2, 7, "">, sym_name = "s5.fd"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.message", lifetime = 1 : i32, name = "message", node_id = 6 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 3, 8, "ferror.sv", 3, 15, "">, semantic_type = !obelisk.string, source_end_column = 15 : i64, source_end_line = 3 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 3, 8, "ferror.sv", 3, 15, "">, sym_name = "s6.message"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.code", lifetime = 1 : i32, name = "code", node_id = 7 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 4, 9, "ferror.sv", 4, 13, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 13 : i64, source_end_line = 4 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 4, 9, "ferror.sv", 4, 13, "">, sym_name = "s7.code"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 8 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 5, 1, "ferror.sv", 9, 4, "">, procedure_kind = 0 : i32, source_end_column = 4 : i64, source_end_line = 9 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 5, 1, "ferror.sv", 9, 4, "">, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 9 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 5, 9, "ferror.sv", 9, 4, "">, source_end_column = 4 : i64, source_end_line = 9 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 5, 9, "ferror.sv", 9, 4, "">} {
            obelisk.sv.statement.list attributes {node_id = 10 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 5, 9, "ferror.sv", 9, 4, "">, source_end_column = 4 : i64, source_end_line = 9 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 5, 9, "ferror.sv", 9, 4, "">} {
              obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 6, 2, "ferror.sv", 6, 34, "">, source_end_column = 34 : i64, source_end_line = 6 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 6, 2, "ferror.sv", 6, 34, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 12 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 6, 2, "ferror.sv", 6, 33, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 33 : i64, source_end_line = 6 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 6, 2, "ferror.sv", 6, 33, "">} {
                  obelisk.sv.expression.named_value attributes {node_id = 13 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 6, 2, "ferror.sv", 6, 4, "">, referenced_path = "top.fd", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.fd, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 4 : i64, source_end_line = 6 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 6, 2, "ferror.sv", 6, 4, "">} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$fopen", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 14 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 6, 7, "ferror.sv", 6, 33, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 33 : i64, source_end_line = 6 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 6, 7, "ferror.sv", 6, 33, "">, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                    obelisk.sv.expression.conversion attributes {node_id = 15 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 6, 14, "ferror.sv", 6, 27, "">, semantic_type = !obelisk.string, source_end_column = 27 : i64, source_end_line = 6 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 6, 14, "ferror.sv", 6, 27, "">} {
                      obelisk.sv.expression.string_literal attributes {constant_value = "scratch.log", node_id = 16 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 6, 14, "ferror.sv", 6, 27, "">, semantic_type = !obelisk.ranged_packed_array<87 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, source_end_column = 27 : i64, source_end_line = 6 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 6, 14, "ferror.sv", 6, 27, "">} {
                      }
                    }
                    obelisk.sv.expression.conversion attributes {node_id = 17 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 6, 29, "ferror.sv", 6, 32, "">, semantic_type = !obelisk.string, source_end_column = 32 : i64, source_end_line = 6 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 6, 29, "ferror.sv", 6, 32, "">} {
                      obelisk.sv.expression.string_literal attributes {constant_value = "w", node_id = 18 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 6, 29, "ferror.sv", 6, 32, "">, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, source_end_column = 32 : i64, source_end_line = 6 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 6, 29, "ferror.sv", 6, 32, "">} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 19 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 7, 2, "ferror.sv", 7, 30, "">, source_end_column = 30 : i64, source_end_line = 7 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 7, 2, "ferror.sv", 7, 30, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 20 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 7, 2, "ferror.sv", 7, 29, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 29 : i64, source_end_line = 7 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 7, 2, "ferror.sv", 7, 29, "">} {
                  obelisk.sv.expression.named_value attributes {node_id = 21 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 7, 2, "ferror.sv", 7, 6, "">, referenced_path = "top.code", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.code, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 6 : i64, source_end_line = 7 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 7, 2, "ferror.sv", 7, 6, "">} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$ferror", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 22 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 7, 9, "ferror.sv", 7, 29, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 29 : i64, source_end_line = 7 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 7, 9, "ferror.sv", 7, 29, "">, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                    obelisk.sv.expression.named_value attributes {node_id = 23 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 7, 17, "ferror.sv", 7, 19, "">, referenced_path = "top.fd", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.fd, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 19 : i64, source_end_line = 7 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 7, 17, "ferror.sv", 7, 19, "">} {
                    }
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 24 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 7, 21, "ferror.sv", 7, 28, "">, semantic_type = !obelisk.string, source_end_column = 28 : i64, source_end_line = 7 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 7, 21, "ferror.sv", 7, 28, "">} {
                      obelisk.sv.expression.named_value attributes {node_id = 25 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 7, 21, "ferror.sv", 7, 28, "">, referenced_path = "top.message", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.message, semantic_type = !obelisk.string, source_end_column = 28 : i64, source_end_line = 7 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 7, 21, "ferror.sv", 7, 28, "">} {
                      }
                      obelisk.sv.expression.empty_argument attributes {node_id = 26 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 7, 21, "ferror.sv", 7, 28, "">, semantic_type = !obelisk.string, source_end_column = 28 : i64, source_end_line = 7 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 7, 21, "ferror.sv", 7, 28, "">} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 27 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 8, 2, "ferror.sv", 8, 14, "">, source_end_column = 14 : i64, source_end_line = 8 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 8, 2, "ferror.sv", 8, 14, "">} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$fclose", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 28 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 8, 2, "ferror.sv", 8, 13, "">, semantic_type = !obelisk.void, source_end_column = 13 : i64, source_end_line = 8 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 8, 2, "ferror.sv", 8, 13, "">, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  obelisk.sv.expression.named_value attributes {node_id = 29 : i64, original_source_range = !obelisk.source_range<"ferror.sv", 8, 10, "ferror.sv", 8, 12, "">, referenced_path = "top.fd", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.fd, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 12 : i64, source_end_line = 8 : i64, source_file = "ferror.sv", source_range = !obelisk.source_range<"ferror.sv", 8, 10, "ferror.sv", 8, 12, "">} {
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

