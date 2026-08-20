// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' -o %t.threaded
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' --mlir-disable-threading -o %t.serial
// RUN: diff %t.threaded %t.serial

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 1, "tmp_sva_unbounded.sv", 8, 10, "">, source_end_column = 10 : i64, source_end_line = 8 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 1, "tmp_sva_unbounded.sv", 8, 10, "">, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 1, "tmp_sva_unbounded.sv", 9, 1, "">, source_end_column = 1 : i64, source_end_line = 9 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 1, "tmp_sva_unbounded.sv", 9, 1, "">, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 8, "tmp_sva_unbounded.sv", 1, 8, "">, referenced_path = "top", referenced_symbol = @s0.top, source_end_column = 8 : i64, source_end_line = 1 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 8, "tmp_sva_unbounded.sv", 1, 8, "">, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 1, "tmp_sva_unbounded.sv", 8, 10, "">, source_end_column = 10 : i64, source_end_line = 8 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 1, "tmp_sva_unbounded.sv", 8, 10, "">, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.clk", name = "clk", node_id = 5 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 24, "tmp_sva_unbounded.sv", 1, 27, "">, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_end_column = 27 : i64, source_end_line = 1 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 24, "tmp_sva_unbounded.sv", 1, 27, "">, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 6 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 24, "tmp_sva_unbounded.sv", 1, 27, "">, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_end_column = 27 : i64, source_end_line = 1 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 24, "tmp_sva_unbounded.sv", 1, 27, "">, sym_name = "s6.clk"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.a", name = "a", node_id = 7 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 29, "tmp_sva_unbounded.sv", 1, 30, "">, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_end_column = 30 : i64, source_end_line = 1 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 29, "tmp_sva_unbounded.sv", 1, 30, "">, sym_name = "s7.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 8 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 29, "tmp_sva_unbounded.sv", 1, 30, "">, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_end_column = 30 : i64, source_end_line = 1 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 29, "tmp_sva_unbounded.sv", 1, 30, "">, sym_name = "s8.a"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.b", name = "b", node_id = 9 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 32, "tmp_sva_unbounded.sv", 1, 33, "">, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_end_column = 33 : i64, source_end_line = 1 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 32, "tmp_sva_unbounded.sv", 1, 33, "">, sym_name = "s9.b"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 10 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 32, "tmp_sva_unbounded.sv", 1, 33, "">, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_end_column = 33 : i64, source_end_line = 1 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 1, 32, "tmp_sva_unbounded.sv", 1, 33, "">, sym_name = "s10.b"} {
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.aa", name = "aa", node_id = 11 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 3, "tmp_sva_unbounded.sv", 3, 63, "">, source_end_column = 63 : i64, source_end_line = 3 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 3, "tmp_sva_unbounded.sv", 3, 63, "">, sym_name = "s11.aa"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 12 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 3, "tmp_sva_unbounded.sv", 3, 63, "">, procedure_kind = 2 : i32, source_end_column = 63 : i64, source_end_line = 3 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 3, "tmp_sva_unbounded.sv", 3, 63, "">, sym_name = "s12", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.aa", block_symbol = @s1.$root::@s3.top::@s4.top::@s11.aa, node_id = 13 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 3, "tmp_sva_unbounded.sv", 3, 63, "">, source_end_column = 63 : i64, source_end_line = 3 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 3, "tmp_sva_unbounded.sv", 3, 63, "">} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 14 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 3, "tmp_sva_unbounded.sv", 3, 63, "">, source_end_column = 63 : i64, source_end_line = 3 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 3, "tmp_sva_unbounded.sv", 3, 63, "">} {
              obelisk.sv.assertion.clocking attributes {node_id = 15 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 24, "tmp_sva_unbounded.sv", 2, 47, "">, source_end_column = 47 : i64, source_end_line = 2 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 24, "tmp_sva_unbounded.sv", 2, 47, "">} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 16 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 26, "tmp_sva_unbounded.sv", 2, 37, "">, source_end_column = 37 : i64, source_end_line = 2 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 26, "tmp_sva_unbounded.sv", 2, 37, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 34, "tmp_sva_unbounded.sv", 2, 37, "">, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_end_column = 37 : i64, source_end_line = 2 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 34, "tmp_sva_unbounded.sv", 2, 37, "">} {
                  }
                }
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 18 : i64, operator_kind = 3 : i32, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 39, "tmp_sva_unbounded.sv", 2, 47, "">, range_is_unbounded = false, source_end_column = 47 : i64, source_end_line = 2 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 39, "tmp_sva_unbounded.sv", 2, 47, "">} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 19 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 46, "tmp_sva_unbounded.sv", 2, 47, "">, repetition_is_unbounded = false, source_end_column = 47 : i64, source_end_line = 2 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 46, "tmp_sva_unbounded.sv", 2, 47, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 20 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 46, "tmp_sva_unbounded.sv", 2, 47, "">, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_end_column = 47 : i64, source_end_line = 2 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 2, 46, "tmp_sva_unbounded.sv", 2, 47, "">} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 21 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 3, 9, "tmp_sva_unbounded.sv", 3, 33, "">, source_end_column = 33 : i64, source_end_line = 3 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 3, 9, "tmp_sva_unbounded.sv", 3, 33, "">} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 22 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 3, 9, "tmp_sva_unbounded.sv", 3, 32, "">, semantic_type = !obelisk.void, source_end_column = 32 : i64, source_end_line = 3 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 3, 9, "tmp_sva_unbounded.sv", 3, 32, "">, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top.aa", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s11.aa} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "always-pass", is_signed = false, node_id = 23 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 3, 18, "tmp_sva_unbounded.sv", 3, 31, "">, semantic_type = !obelisk.ranged_packed_array<87 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, source_end_column = 31 : i64, source_end_line = 3 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 3, 18, "tmp_sva_unbounded.sv", 3, 31, "">} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 24 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 3, 39, "tmp_sva_unbounded.sv", 3, 63, "">, source_end_column = 63 : i64, source_end_line = 3 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 3, 39, "tmp_sva_unbounded.sv", 3, 63, "">} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 25 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 3, 39, "tmp_sva_unbounded.sv", 3, 62, "">, semantic_type = !obelisk.void, source_end_column = 62 : i64, source_end_line = 3 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 3, 39, "tmp_sva_unbounded.sv", 3, 62, "">, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top.aa", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s11.aa} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "always-fail", is_signed = false, node_id = 26 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 3, 48, "tmp_sva_unbounded.sv", 3, 61, "">, semantic_type = !obelisk.ranged_packed_array<87 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, source_end_column = 61 : i64, source_end_line = 3 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 3, 48, "tmp_sva_unbounded.sv", 3, 61, "">} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.ar", name = "ar", node_id = 27 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 3, "tmp_sva_unbounded.sv", 4, 55, "">, source_end_column = 55 : i64, source_end_line = 4 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 3, "tmp_sva_unbounded.sv", 4, 55, "">, sym_name = "s13.ar"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 28 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 3, "tmp_sva_unbounded.sv", 4, 55, "">, procedure_kind = 2 : i32, source_end_column = 55 : i64, source_end_line = 4 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 3, "tmp_sva_unbounded.sv", 4, 55, "">, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.ar", block_symbol = @s1.$root::@s3.top::@s4.top::@s13.ar, node_id = 29 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 3, "tmp_sva_unbounded.sv", 4, 55, "">, source_end_column = 55 : i64, source_end_line = 4 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 3, "tmp_sva_unbounded.sv", 4, 55, "">} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 30 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 3, "tmp_sva_unbounded.sv", 4, 55, "">, source_end_column = 55 : i64, source_end_line = 4 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 3, "tmp_sva_unbounded.sv", 4, 55, "">} {
              obelisk.sv.assertion.clocking attributes {node_id = 31 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 24, "tmp_sva_unbounded.sv", 4, 53, "">, source_end_column = 53 : i64, source_end_line = 4 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 24, "tmp_sva_unbounded.sv", 4, 53, "">} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 32 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 26, "tmp_sva_unbounded.sv", 4, 37, "">, source_end_column = 37 : i64, source_end_line = 4 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 26, "tmp_sva_unbounded.sv", 4, 37, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 33 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 34, "tmp_sva_unbounded.sv", 4, 37, "">, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_end_column = 37 : i64, source_end_line = 4 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 34, "tmp_sva_unbounded.sv", 4, 37, "">} {
                  }
                }
                obelisk.sv.assertion.unary attributes {has_range = true, node_id = 34 : i64, operator_kind = 3 : i32, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 39, "tmp_sva_unbounded.sv", 4, 53, "">, range_is_unbounded = true, range_min = 2 : i64, source_end_column = 53 : i64, source_end_line = 4 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 39, "tmp_sva_unbounded.sv", 4, 53, "">} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 35 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 52, "tmp_sva_unbounded.sv", 4, 53, "">, repetition_is_unbounded = false, source_end_column = 53 : i64, source_end_line = 4 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 52, "tmp_sva_unbounded.sv", 4, 53, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 36 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 52, "tmp_sva_unbounded.sv", 4, 53, "">, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_end_column = 53 : i64, source_end_line = 4 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 52, "tmp_sva_unbounded.sv", 4, 53, "">} {
                    }
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 37 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 54, "tmp_sva_unbounded.sv", 4, 55, "">, source_end_column = 55 : i64, source_end_line = 4 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 4, 54, "tmp_sva_unbounded.sv", 4, 55, "">} {
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.ee", name = "ee", node_id = 38 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 3, "tmp_sva_unbounded.sv", 6, 71, "">, source_end_column = 71 : i64, source_end_line = 6 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 3, "tmp_sva_unbounded.sv", 6, 71, "">, sym_name = "s15.ee"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 39 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 3, "tmp_sva_unbounded.sv", 6, 71, "">, procedure_kind = 2 : i32, source_end_column = 71 : i64, source_end_line = 6 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 3, "tmp_sva_unbounded.sv", 6, 71, "">, sym_name = "s16", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.ee", block_symbol = @s1.$root::@s3.top::@s4.top::@s15.ee, node_id = 40 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 3, "tmp_sva_unbounded.sv", 6, 71, "">, source_end_column = 71 : i64, source_end_line = 6 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 3, "tmp_sva_unbounded.sv", 6, 71, "">} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 41 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 3, "tmp_sva_unbounded.sv", 6, 71, "">, source_end_column = 71 : i64, source_end_line = 6 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 3, "tmp_sva_unbounded.sv", 6, 71, "">} {
              obelisk.sv.assertion.clocking attributes {node_id = 42 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 24, "tmp_sva_unbounded.sv", 5, 53, "">, source_end_column = 53 : i64, source_end_line = 5 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 24, "tmp_sva_unbounded.sv", 5, 53, "">} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 43 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 26, "tmp_sva_unbounded.sv", 5, 37, "">, source_end_column = 37 : i64, source_end_line = 5 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 26, "tmp_sva_unbounded.sv", 5, 37, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 44 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 34, "tmp_sva_unbounded.sv", 5, 37, "">, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_end_column = 37 : i64, source_end_line = 5 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 34, "tmp_sva_unbounded.sv", 5, 37, "">} {
                  }
                }
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 45 : i64, operator_kind = 6 : i32, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 39, "tmp_sva_unbounded.sv", 5, 53, "">, range_is_unbounded = false, source_end_column = 53 : i64, source_end_line = 5 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 39, "tmp_sva_unbounded.sv", 5, 53, "">} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 46 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 52, "tmp_sva_unbounded.sv", 5, 53, "">, repetition_is_unbounded = false, source_end_column = 53 : i64, source_end_line = 5 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 52, "tmp_sva_unbounded.sv", 5, 53, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 47 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 52, "tmp_sva_unbounded.sv", 5, 53, "">, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_end_column = 53 : i64, source_end_line = 5 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 5, 52, "tmp_sva_unbounded.sv", 5, 53, "">} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 48 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 6, 9, "tmp_sva_unbounded.sv", 6, 37, "">, source_end_column = 37 : i64, source_end_line = 6 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 6, 9, "tmp_sva_unbounded.sv", 6, 37, "">} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 49 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 6, 9, "tmp_sva_unbounded.sv", 6, 36, "">, semantic_type = !obelisk.void, source_end_column = 36 : i64, source_end_line = 6 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 6, 9, "tmp_sva_unbounded.sv", 6, 36, "">, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top.ee", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s15.ee} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "eventually-pass", is_signed = false, node_id = 50 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 6, 18, "tmp_sva_unbounded.sv", 6, 35, "">, semantic_type = !obelisk.ranged_packed_array<119 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, source_end_column = 35 : i64, source_end_line = 6 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 6, 18, "tmp_sva_unbounded.sv", 6, 35, "">} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 51 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 6, 43, "tmp_sva_unbounded.sv", 6, 71, "">, source_end_column = 71 : i64, source_end_line = 6 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 6, 43, "tmp_sva_unbounded.sv", 6, 71, "">} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 52 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 6, 43, "tmp_sva_unbounded.sv", 6, 70, "">, semantic_type = !obelisk.void, source_end_column = 70 : i64, source_end_line = 6 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 6, 43, "tmp_sva_unbounded.sv", 6, 70, "">, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top.ee", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s15.ee} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "eventually-fail", is_signed = false, node_id = 53 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 6, 52, "tmp_sva_unbounded.sv", 6, 69, "">, semantic_type = !obelisk.ranged_packed_array<119 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, source_end_column = 69 : i64, source_end_line = 6 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 6, 52, "tmp_sva_unbounded.sv", 6, 69, "">} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top.er", name = "er", node_id = 54 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 3, "tmp_sva_unbounded.sv", 7, 61, "">, source_end_column = 61 : i64, source_end_line = 7 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 3, "tmp_sva_unbounded.sv", 7, 61, "">, sym_name = "s17.er"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 55 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 3, "tmp_sva_unbounded.sv", 7, 61, "">, procedure_kind = 2 : i32, source_end_column = 61 : i64, source_end_line = 7 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 3, "tmp_sva_unbounded.sv", 7, 61, "">, sym_name = "s18", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.er", block_symbol = @s1.$root::@s3.top::@s4.top::@s17.er, node_id = 56 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 3, "tmp_sva_unbounded.sv", 7, 61, "">, source_end_column = 61 : i64, source_end_line = 7 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 3, "tmp_sva_unbounded.sv", 7, 61, "">} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 57 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 3, "tmp_sva_unbounded.sv", 7, 61, "">, source_end_column = 61 : i64, source_end_line = 7 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 3, "tmp_sva_unbounded.sv", 7, 61, "">} {
              obelisk.sv.assertion.clocking attributes {node_id = 58 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 24, "tmp_sva_unbounded.sv", 7, 59, "">, source_end_column = 59 : i64, source_end_line = 7 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 24, "tmp_sva_unbounded.sv", 7, 59, "">} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 59 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 26, "tmp_sva_unbounded.sv", 7, 37, "">, source_end_column = 37 : i64, source_end_line = 7 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 26, "tmp_sva_unbounded.sv", 7, 37, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 60 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 34, "tmp_sva_unbounded.sv", 7, 37, "">, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_end_column = 37 : i64, source_end_line = 7 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 34, "tmp_sva_unbounded.sv", 7, 37, "">} {
                  }
                }
                obelisk.sv.assertion.unary attributes {has_range = true, node_id = 61 : i64, operator_kind = 6 : i32, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 39, "tmp_sva_unbounded.sv", 7, 59, "">, range_is_unbounded = true, range_min = 2 : i64, source_end_column = 59 : i64, source_end_line = 7 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 39, "tmp_sva_unbounded.sv", 7, 59, "">} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 62 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 58, "tmp_sva_unbounded.sv", 7, 59, "">, repetition_is_unbounded = false, source_end_column = 59 : i64, source_end_line = 7 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 58, "tmp_sva_unbounded.sv", 7, 59, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 63 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 58, "tmp_sva_unbounded.sv", 7, 59, "">, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_end_column = 59 : i64, source_end_line = 7 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 58, "tmp_sva_unbounded.sv", 7, 59, "">} {
                    }
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 64 : i64, original_source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 60, "tmp_sva_unbounded.sv", 7, 61, "">, source_end_column = 61 : i64, source_end_line = 7 : i64, source_file = "tmp_sva_unbounded.sv", source_range = !obelisk.source_range<"tmp_sva_unbounded.sv", 7, 60, "tmp_sva_unbounded.sv", 7, 61, "">} {
              }
            }
          }
        }
      }
    }
  }
}


// Weak always keeps one eligible count. False samples fail all eligible
// attempts; finite end-of-simulation succeeds the survivors through an Active
// final coordinator which dispatches the pass action in Reactive.
// CHECK: function = @unit_0.$concurrent_eos_count.14.always, block = 0, region = active
// CHECK: function = @unit_0.fork.14.0.0, block = 0, region = reactive
// CHECK: function = @unit_2.$concurrent_eos_count.41.s_eventually, block = 0, region = active
// CHECK-LABEL: obelisk_sim.func private @unit_0.fork.14.0.0(
// CHECK: obelisk_sim.bytes.constant "always-pass"
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_eos_count.14.always(
// CHECK-SAME: entry_kind = 2 : i32
// CHECK-SAME: home_region = 2 : i32
// CHECK-SAME: obelisk_sim.concurrent_eos_counted
// CHECK: obelisk_sim.ref.load
// CHECK: cf.cond_br
// CHECK: obelisk_sim.spawn @unit_0.fork.14.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.persistent_unary_aggregate_tokens
// CHECK-SAME: obelisk_sim.persistent_unary_kind = "always"
// CHECK-SAME: obelisk_sim.persistent_unary_minimum = 0 : i64
// CHECK: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_0.$concurrent_eos_count.14.always
// CHECK: obelisk_sim.assert.sampled_read
// CHECK: arith.select

// The [2:$] warm-up is one age bitset, not one process per attempt. This also
// permits holes between starts when assertion checking is disabled.
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.persistent_unary_kind = "always"
// CHECK-SAME: obelisk_sim.persistent_unary_minimum = 2 : i64
// CHECK: arith.constant 2 : i64
// CHECK: cf.br ^bb1({{.*}} : i64, i64)
// CHECK: [[MATURE:%.*]] = arith.andi {{.*}}
// CHECK: arith.cmpi ne, [[MATURE]],
// CHECK: [[SHIFTED:%.*]] = arith.shli
// CHECK: [[RETAINED:%.*]] = arith.andi [[SHIFTED]],
// CHECK: arith.ori [[RETAINED]],

// Strong eventually succeeds every eligible attempt together on a true
// sample and fails every still-live attempt through the final coordinator.
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_eos_count.41.s_eventually(
// CHECK-SAME: entry_kind = 2 : i32
// CHECK-SAME: home_region = 2 : i32
// CHECK: obelisk_sim.spawn @unit_2.fork.41.1.1
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.persistent_unary_kind = "s_eventually"
// CHECK-SAME: obelisk_sim.persistent_unary_minimum = 0 : i64
// CHECK: obelisk_sim.spawn @unit_2.$concurrent_eos_count.41.s_eventually
// CHECK: obelisk_sim.assert.sampled_read
// CHECK: arith.select

// A ranged strong eventuality adds the same age bitset; final failure counts
// eligible attempts plus the set immature bits exactly.
// CHECK-LABEL: obelisk_sim.func private @unit_3.$concurrent_eos_count.57.s_eventually(
// CHECK: [[ELIGIBLE:%.*]] = obelisk_sim.ref.load %arg1
// CHECK: [[IMMATURE:%.*]] = obelisk_sim.ref.load %arg2
// CHECK: cf.br [[POP_LOOP:\^bb[0-9]+]]([[IMMATURE]], [[ELIGIBLE]] : i64, i64)
// CHECK: [[POP_LOOP]]([[BITS:%.*]]: i64, [[COUNT:%.*]]: i64)
// CHECK: [[LESS_ONE:%.*]] = arith.subi [[BITS]],
// CHECK: [[NEXT_BITS:%.*]] = arith.andi [[BITS]], [[LESS_ONE]] : i64
// CHECK: [[NEXT_COUNT:%.*]] = arith.addi [[COUNT]],
// CHECK: cf.br [[POP_LOOP]]([[NEXT_BITS]], [[NEXT_COUNT]] : i64, i64)
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: obelisk_sim.persistent_unary_kind = "s_eventually"
// CHECK-SAME: obelisk_sim.persistent_unary_minimum = 2 : i64
// CHECK-COUNT-2: obelisk_sim.ref.alloc
// CHECK: arith.constant {{.*}}2 : i64
// CHECK: arith.cmpi ne
