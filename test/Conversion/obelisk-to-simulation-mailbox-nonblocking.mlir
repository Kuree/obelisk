// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
//
// CHECK: !obelisk_sim.mailbox<!obelisk_sim.string>
// CHECK: obelisk_sim.mailbox.create
// CHECK: obelisk_sim.mailbox.try_put
// CHECK: obelisk_sim.mailbox.try_peek
// CHECK: obelisk_sim.mailbox.try_get
// CHECK: obelisk_sim.mailbox.num

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 1, 1, "../../../../tmp/mailbox-nonblocking-probe.sv", 14, 10, "">, source_end_column = 10 : i64, source_end_line = 14 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 1, 1, "../../../../tmp/mailbox-nonblocking-probe.sv", 14, 10, "">, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 1, 1, "../../../../tmp/mailbox-nonblocking-probe.sv", 15, 1, "">, source_end_column = 1 : i64, source_end_line = 15 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 1, 1, "../../../../tmp/mailbox-nonblocking-probe.sv", 15, 1, "">, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 1, 8, "../../../../tmp/mailbox-nonblocking-probe.sv", 1, 8, "">, referenced_path = "top", referenced_symbol = @s0.top, source_end_column = 8 : i64, source_end_line = 1 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 1, 8, "../../../../tmp/mailbox-nonblocking-probe.sv", 1, 8, "">, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 1, 1, "../../../../tmp/mailbox-nonblocking-probe.sv", 14, 10, "">, source_end_column = 10 : i64, source_end_line = 14 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 1, 1, "../../../../tmp/mailbox-nonblocking-probe.sv", 14, 10, "">, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.m", lifetime = 1 : i32, name = "m", node_id = 5 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 2, 21, "../../../../tmp/mailbox-nonblocking-probe.sv", 2, 22, "">, semantic_type = !obelisk.class_handle<@s7.std::@s6.mailbox>, source_end_column = 22 : i64, source_end_line = 2 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 2, 21, "../../../../tmp/mailbox-nonblocking-probe.sv", 2, 22, "">, sym_name = "s5.m"} {
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top", node_id = 6 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 3, 11, "../../../../tmp/mailbox-nonblocking-probe.sv", 13, 6, "">, source_end_column = 6 : i64, source_end_line = 13 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 3, 11, "../../../../tmp/mailbox-nonblocking-probe.sv", 13, 6, "">, sym_name = "s8"} {
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.input_value", lifetime = 1 : i32, name = "input_value", node_id = 7 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 4, 12, "../../../../tmp/mailbox-nonblocking-probe.sv", 4, 31, "">, semantic_type = !obelisk.string, source_end_column = 31 : i64, source_end_line = 4 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 4, 12, "../../../../tmp/mailbox-nonblocking-probe.sv", 4, 31, "">, sym_name = "s9.input_value"} {
            obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 8 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 4, 26, "../../../../tmp/mailbox-nonblocking-probe.sv", 4, 31, "">, semantic_type = !obelisk.string, source_end_column = 31 : i64, source_end_line = 4 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 4, 26, "../../../../tmp/mailbox-nonblocking-probe.sv", 4, 31, "">} {
              obelisk.sv.expression.string_literal attributes {constant_value = "abc", is_signed = false, node_id = 9 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 4, 26, "../../../../tmp/mailbox-nonblocking-probe.sv", 4, 31, "">, semantic_type = !obelisk.ranged_packed_array<23 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, source_end_column = 31 : i64, source_end_line = 4 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 4, 26, "../../../../tmp/mailbox-nonblocking-probe.sv", 4, 31, "">} {
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.peeked", lifetime = 1 : i32, name = "peeked", node_id = 10 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 5, 12, "../../../../tmp/mailbox-nonblocking-probe.sv", 5, 18, "">, semantic_type = !obelisk.string, source_end_column = 18 : i64, source_end_line = 5 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 5, 12, "../../../../tmp/mailbox-nonblocking-probe.sv", 5, 18, "">, sym_name = "s10.peeked"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.received", lifetime = 1 : i32, name = "received", node_id = 11 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 6, 12, "../../../../tmp/mailbox-nonblocking-probe.sv", 6, 20, "">, semantic_type = !obelisk.string, source_end_column = 20 : i64, source_end_line = 6 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 6, 12, "../../../../tmp/mailbox-nonblocking-probe.sv", 6, 20, "">, sym_name = "s11.received"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.ok", lifetime = 1 : i32, name = "ok", node_id = 12 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 7, 9, "../../../../tmp/mailbox-nonblocking-probe.sv", 7, 11, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 11 : i64, source_end_line = 7 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 7, 9, "../../../../tmp/mailbox-nonblocking-probe.sv", 7, 11, "">, sym_name = "s12.ok"} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 13 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 3, 3, "../../../../tmp/mailbox-nonblocking-probe.sv", 13, 6, "">, procedure_kind = 0 : i32, source_end_column = 6 : i64, source_end_line = 13 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 3, 3, "../../../../tmp/mailbox-nonblocking-probe.sv", 13, 6, "">, sym_name = "s13", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 14 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 3, 11, "../../../../tmp/mailbox-nonblocking-probe.sv", 13, 6, "">, source_end_column = 6 : i64, source_end_line = 13 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 3, 11, "../../../../tmp/mailbox-nonblocking-probe.sv", 13, 6, "">} {
            obelisk.sv.statement.list attributes {node_id = 15 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 3, 11, "../../../../tmp/mailbox-nonblocking-probe.sv", 13, 6, "">, source_end_column = 6 : i64, source_end_line = 13 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 3, 11, "../../../../tmp/mailbox-nonblocking-probe.sv", 13, 6, "">} {
              obelisk.sv.statement.variable_declaration attributes {node_id = 16 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 4, 12, "../../../../tmp/mailbox-nonblocking-probe.sv", 4, 23, "">, referenced_path = "top.input_value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8::@s9.input_value, source_end_column = 23 : i64, source_end_line = 4 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 4, 12, "../../../../tmp/mailbox-nonblocking-probe.sv", 4, 23, "">} {
              }
              obelisk.sv.statement.variable_declaration attributes {node_id = 17 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 5, 12, "../../../../tmp/mailbox-nonblocking-probe.sv", 5, 18, "">, referenced_path = "top.peeked", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8::@s10.peeked, source_end_column = 18 : i64, source_end_line = 5 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 5, 12, "../../../../tmp/mailbox-nonblocking-probe.sv", 5, 18, "">} {
              }
              obelisk.sv.statement.variable_declaration attributes {node_id = 18 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 6, 12, "../../../../tmp/mailbox-nonblocking-probe.sv", 6, 20, "">, referenced_path = "top.received", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8::@s11.received, source_end_column = 20 : i64, source_end_line = 6 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 6, 12, "../../../../tmp/mailbox-nonblocking-probe.sv", 6, 20, "">} {
              }
              obelisk.sv.statement.variable_declaration attributes {node_id = 19 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 7, 9, "../../../../tmp/mailbox-nonblocking-probe.sv", 7, 11, "">, referenced_path = "top.ok", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8::@s12.ok, source_end_column = 11 : i64, source_end_line = 7 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 7, 9, "../../../../tmp/mailbox-nonblocking-probe.sv", 7, 11, "">} {
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 20 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 8, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 8, 16, "">, source_end_column = 16 : i64, source_end_line = 8 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 8, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 8, 16, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 21 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 8, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 8, 15, "">, semantic_type = !obelisk.class_handle<@s7.std::@s6.mailbox>, source_end_column = 15 : i64, source_end_line = 8 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 8, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 8, 15, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 8, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 8, 6, "">, referenced_path = "top.m", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.m, semantic_type = !obelisk.class_handle<@s7.std::@s6.mailbox>, source_end_column = 6 : i64, source_end_line = 8 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 8, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 8, 6, "">} {
                  }
                  obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 23 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 8, 9, "../../../../tmp/mailbox-nonblocking-probe.sv", 8, 15, "">, semantic_type = !obelisk.class_handle<@s7.std::@s6.mailbox>, source_end_column = 15 : i64, source_end_line = 8 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 8, 9, "../../../../tmp/mailbox-nonblocking-probe.sv", 8, 15, "">} {
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "new", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 24 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 8, 9, "../../../../tmp/mailbox-nonblocking-probe.sv", 8, 15, "">, referenced_path = "std::mailbox#(string)::new", referenced_symbol = @s7.std::@s41.mailbox::@s6.mailbox::@s43.new, semantic_type = !obelisk.void, source_end_column = 15 : i64, source_end_line = 8 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 8, 9, "../../../../tmp/mailbox-nonblocking-probe.sv", 8, 15, "">, subroutine_kind = 0 : i32} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 25 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 8, 13, "../../../../tmp/mailbox-nonblocking-probe.sv", 8, 14, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 14 : i64, source_end_line = 8 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 8, 13, "../../../../tmp/mailbox-nonblocking-probe.sv", 8, 14, "">} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 26 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 9, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 9, 33, "">, source_end_column = 33 : i64, source_end_line = 9 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 9, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 9, 33, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 27 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 9, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 9, 32, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 32 : i64, source_end_line = 9 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 9, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 9, 32, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 28 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 9, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 9, 7, "">, referenced_path = "top.ok", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8::@s12.ok, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 7 : i64, source_end_line = 9 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 9, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 9, 7, "">} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "try_put", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = true, is_super_class = false, is_system_call = false, node_id = 29 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 9, 10, "../../../../tmp/mailbox-nonblocking-probe.sv", 9, 19, "">, referenced_path = "std::mailbox#(string)::try_put", referenced_symbol = @s7.std::@s41.mailbox::@s6.mailbox::@s48.try_put, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 19 : i64, source_end_line = 9 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 9, 10, "../../../../tmp/mailbox-nonblocking-probe.sv", 9, 19, "">, subroutine_kind = 0 : i32} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 30 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 9, 10, "../../../../tmp/mailbox-nonblocking-probe.sv", 9, 11, "">, referenced_path = "top.m", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.m, semantic_type = !obelisk.class_handle<@s7.std::@s6.mailbox>, source_end_column = 11 : i64, source_end_line = 9 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 9, 10, "../../../../tmp/mailbox-nonblocking-probe.sv", 9, 11, "">} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 31 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 9, 20, "../../../../tmp/mailbox-nonblocking-probe.sv", 9, 31, "">, referenced_path = "top.input_value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8::@s9.input_value, semantic_type = !obelisk.string, source_end_column = 31 : i64, source_end_line = 9 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 9, 20, "../../../../tmp/mailbox-nonblocking-probe.sv", 9, 31, "">} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 32 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 10, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 10, 29, "">, source_end_column = 29 : i64, source_end_line = 10 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 10, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 10, 29, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 33 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 10, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 10, 28, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 28 : i64, source_end_line = 10 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 10, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 10, 28, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 34 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 10, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 10, 7, "">, referenced_path = "top.ok", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8::@s12.ok, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 7 : i64, source_end_line = 10 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 10, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 10, 7, "">} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "try_peek", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = true, is_signed = true, is_super_class = false, is_system_call = false, node_id = 35 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 10, 10, "../../../../tmp/mailbox-nonblocking-probe.sv", 10, 20, "">, referenced_path = "std::mailbox#(string)::try_peek", referenced_symbol = @s7.std::@s41.mailbox::@s6.mailbox::@s56.try_peek, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 20 : i64, source_end_line = 10 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 10, 10, "../../../../tmp/mailbox-nonblocking-probe.sv", 10, 20, "">, subroutine_kind = 0 : i32} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 36 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 10, 10, "../../../../tmp/mailbox-nonblocking-probe.sv", 10, 11, "">, referenced_path = "top.m", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.m, semantic_type = !obelisk.class_handle<@s7.std::@s6.mailbox>, source_end_column = 11 : i64, source_end_line = 10 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 10, 10, "../../../../tmp/mailbox-nonblocking-probe.sv", 10, 11, "">} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 37 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 10, 21, "../../../../tmp/mailbox-nonblocking-probe.sv", 10, 27, "">, referenced_path = "top.peeked", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8::@s10.peeked, semantic_type = !obelisk.string, source_end_column = 27 : i64, source_end_line = 10 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 10, 21, "../../../../tmp/mailbox-nonblocking-probe.sv", 10, 27, "">} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 38 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 11, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 11, 30, "">, source_end_column = 30 : i64, source_end_line = 11 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 11, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 11, 30, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 39 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 11, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 11, 29, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 29 : i64, source_end_line = 11 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 11, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 11, 29, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 40 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 11, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 11, 7, "">, referenced_path = "top.ok", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8::@s12.ok, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 7 : i64, source_end_line = 11 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 11, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 11, 7, "">} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "try_get", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = true, is_signed = true, is_super_class = false, is_system_call = false, node_id = 41 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 11, 10, "../../../../tmp/mailbox-nonblocking-probe.sv", 11, 19, "">, referenced_path = "std::mailbox#(string)::try_get", referenced_symbol = @s7.std::@s41.mailbox::@s6.mailbox::@s52.try_get, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 19 : i64, source_end_line = 11 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 11, 10, "../../../../tmp/mailbox-nonblocking-probe.sv", 11, 19, "">, subroutine_kind = 0 : i32} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 42 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 11, 10, "../../../../tmp/mailbox-nonblocking-probe.sv", 11, 11, "">, referenced_path = "top.m", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.m, semantic_type = !obelisk.class_handle<@s7.std::@s6.mailbox>, source_end_column = 11 : i64, source_end_line = 11 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 11, 10, "../../../../tmp/mailbox-nonblocking-probe.sv", 11, 11, "">} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 43 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 11, 20, "../../../../tmp/mailbox-nonblocking-probe.sv", 11, 28, "">, referenced_path = "top.received", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8::@s11.received, semantic_type = !obelisk.string, source_end_column = 28 : i64, source_end_line = 11 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 11, 20, "../../../../tmp/mailbox-nonblocking-probe.sv", 11, 28, "">} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 44 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 12, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 12, 18, "">, source_end_column = 18 : i64, source_end_line = 12 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 12, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 12, 18, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 45 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 12, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 12, 17, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 17 : i64, source_end_line = 12 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 12, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 12, 17, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 46 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 12, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 12, 7, "">, referenced_path = "top.ok", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8::@s12.ok, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 7 : i64, source_end_line = 12 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 12, 5, "../../../../tmp/mailbox-nonblocking-probe.sv", 12, 7, "">} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "num", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = true, is_super_class = false, is_system_call = false, node_id = 47 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 12, 10, "../../../../tmp/mailbox-nonblocking-probe.sv", 12, 15, "">, referenced_path = "std::mailbox#(string)::num", referenced_symbol = @s7.std::@s41.mailbox::@s6.mailbox::@s45.num, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 15 : i64, source_end_line = 12 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 12, 10, "../../../../tmp/mailbox-nonblocking-probe.sv", 12, 15, "">, subroutine_kind = 0 : i32} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 48 : i64, original_source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 12, 10, "../../../../tmp/mailbox-nonblocking-probe.sv", 12, 11, "">, referenced_path = "top.m", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.m, semantic_type = !obelisk.class_handle<@s7.std::@s6.mailbox>, source_end_column = 11 : i64, source_end_line = 12 : i64, source_file = "../../../../tmp/mailbox-nonblocking-probe.sv", source_range = !obelisk.source_range<"../../../../tmp/mailbox-nonblocking-probe.sv", 12, 10, "../../../../tmp/mailbox-nonblocking-probe.sv", 12, 11, "">} {
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
  obelisk.sv.symbol.package attributes {hierarchical_name = "std", name = "std", node_id = 49 : i64, sym_name = "s7.std"} {
    obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "std::process", implemented_interfaces = [], is_abstract = true, is_final = true, is_interface = false, is_uninstantiated = false, name = "process", node_id = 50 : i64, semantic_type = !obelisk.class_handle<@s7.std::@s14.process>, sym_name = "s14.process"} {
      obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "std::process::FINISHED", name = "FINISHED", node_id = 51 : i64, sym_name = "s15.FINISHED"} {
      }
      obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "std::process::RUNNING", name = "RUNNING", node_id = 52 : i64, sym_name = "s16.RUNNING"} {
      }
      obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "std::process::WAITING", name = "WAITING", node_id = 53 : i64, sym_name = "s17.WAITING"} {
      }
      obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "std::process::SUSPENDED", name = "SUSPENDED", node_id = 54 : i64, sym_name = "s18.SUSPENDED"} {
      }
      obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "std::process::KILLED", name = "KILLED", node_id = 55 : i64, sym_name = "s19.KILLED"} {
      }
      obelisk.sv.type.type_alias attributes {hierarchical_name = "std::process::state", name = "state", node_id = 56 : i64, semantic_type = !obelisk.enum<"std::process", !obelisk.integral<32, true, false, 31 : 0, int>>, sym_name = "s20.state"} {
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::process::self", is_builtin, is_static, name = "self", node_id = 57 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.class_handle<@s7.std::@s14.process>, false>, subroutine_kind = 0 : i32, sym_name = "s21.self", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 58 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::process::status", is_builtin, name = "status", node_id = 59 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.enum<"std::process", !obelisk.integral<32, true, false, 31 : 0, int>>, false>, subroutine_kind = 0 : i32, sym_name = "s22.status", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 60 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::process::kill", is_builtin, name = "kill", node_id = 61 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s23.kill", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 62 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::process::await", is_builtin, name = "await", node_id = 63 : i64, semantic_type = !obelisk.subroutine<() -> (), true>, subroutine_kind = 1 : i32, sym_name = "s24.await", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 64 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::process::suspend", is_builtin, name = "suspend", node_id = 65 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s25.suspend", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 66 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::process::resume", is_builtin, name = "resume", node_id = 67 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s26.resume", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 68 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::process::get_randstate", is_builtin, name = "get_randstate", node_id = 69 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s27.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 70 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::process::srandom", is_builtin, name = "srandom", node_id = 71 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s28.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 72 : i64} {
        }
        obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "std::process::srandom.seed", name = "seed", node_id = 73 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s29.seed"} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::process::set_randstate", is_builtin, name = "set_randstate", node_id = 74 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s30.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 75 : i64} {
        }
        obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "std::process::set_randstate.state", name = "state", node_id = 76 : i64, semantic_type = !obelisk.string, sym_name = "s31.state"} {
        }
      }
    }
    obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, constructor_path = "std::semaphore::new", constructor_symbol = @s7.std::@s32.semaphore::@s33.new, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "std::semaphore", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "semaphore", node_id = 77 : i64, semantic_type = !obelisk.class_handle<@s7.std::@s32.semaphore>, sym_name = "s32.semaphore"} {
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::semaphore::new", is_builtin, is_constructor, name = "new", node_id = 78 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s33.new", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 79 : i64} {
        }
        obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "std::semaphore::new.keyCount", name = "keyCount", node_id = 80 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s34.keyCount"} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 81 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::semaphore::put", is_builtin, name = "put", node_id = 82 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s35.put", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 83 : i64} {
        }
        obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "std::semaphore::put.keyCount", name = "keyCount", node_id = 84 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s36.keyCount"} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 85 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::semaphore::get", is_builtin, name = "get", node_id = 86 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> (), true>, subroutine_kind = 1 : i32, sym_name = "s37.get", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 87 : i64} {
        }
        obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "std::semaphore::get.keyCount", name = "keyCount", node_id = 88 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s38.keyCount"} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 89 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::semaphore::try_get", is_builtin, name = "try_get", node_id = 90 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s39.try_get", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 91 : i64} {
        }
        obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "std::semaphore::try_get.keyCount", name = "keyCount", node_id = 92 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s40.keyCount"} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 93 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
    }
    obelisk.sv.symbol.generic_class_def attributes {hierarchical_name = "std::mailbox", is_interface = false, name = "mailbox", node_id = 94 : i64, specialization_count = 1 : i64, sym_name = "s41.mailbox"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, constructor_path = "std::mailbox#(string)::new", constructor_symbol = @s7.std::@s41.mailbox::@s6.mailbox::@s43.new, declared_interfaces = [], generic_class_path = "std::mailbox", generic_class_symbol = @s7.std::@s41.mailbox, generic_parameter_paths = ["std::mailbox#(string)::T"], generic_parameter_symbols = [@s7.std::@s6.mailbox::@s42.T], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "std::mailbox#(string)", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "mailbox", node_id = 95 : i64, semantic_type = !obelisk.class_handle<@s7.std::@s6.mailbox>, sym_name = "s6.mailbox"} {
        obelisk.sv.symbol.type_parameter attributes {hierarchical_name = "std::mailbox#(string)::T", name = "T", node_id = 96 : i64, semantic_type = !obelisk.string, sym_name = "s42.T"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::mailbox#(string)::new", is_builtin, is_constructor, name = "new", node_id = 97 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s43.new", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 98 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "std::mailbox#(string)::new.bound", name = "bound", node_id = 99 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s44.bound"} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 100 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::mailbox#(string)::num", is_builtin, name = "num", node_id = 101 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s45.num", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 102 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::mailbox#(string)::put", is_builtin, name = "put", node_id = 103 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> (), true>, subroutine_kind = 1 : i32, sym_name = "s46.put", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 104 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "std::mailbox#(string)::put.message", name = "message", node_id = 105 : i64, semantic_type = !obelisk.string, sym_name = "s47.message"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::mailbox#(string)::try_put", is_builtin, name = "try_put", node_id = 106 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s48.try_put", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 107 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "std::mailbox#(string)::try_put.message", name = "message", node_id = 108 : i64, semantic_type = !obelisk.string, sym_name = "s49.message"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::mailbox#(string)::get", is_builtin, name = "get", node_id = 109 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> (), true>, subroutine_kind = 1 : i32, sym_name = "s50.get", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 110 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "std::mailbox#(string)::get.message", name = "message", node_id = 111 : i64, semantic_type = !obelisk.string, sym_name = "s51.message"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::mailbox#(string)::try_get", is_builtin, name = "try_get", node_id = 112 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s52.try_get", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 113 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "std::mailbox#(string)::try_get.message", name = "message", node_id = 114 : i64, semantic_type = !obelisk.string, sym_name = "s53.message"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::mailbox#(string)::peek", is_builtin, name = "peek", node_id = 115 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> (), true>, subroutine_kind = 1 : i32, sym_name = "s54.peek", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 116 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "std::mailbox#(string)::peek.message", name = "message", node_id = 117 : i64, semantic_type = !obelisk.string, sym_name = "s55.message"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::mailbox#(string)::try_peek", is_builtin, name = "try_peek", node_id = 118 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s56.try_peek", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 119 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "std::mailbox#(string)::try_peek.message", name = "message", node_id = 120 : i64, semantic_type = !obelisk.string, sym_name = "s57.message"} {
          }
        }
      }
    }
    obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::randomize", is_builtin, is_randomize, name = "randomize", node_id = 121 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s58.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
      obelisk.sv.statement.list attributes {node_id = 122 : i64} {
      }
    }
    obelisk.sv.symbol.generic_class_def attributes {hierarchical_name = "std::weak_reference", is_interface = false, name = "weak_reference", node_id = 123 : i64, specialization_count = 0 : i64, sym_name = "s59.weak_reference"} {
    }
  }
}

