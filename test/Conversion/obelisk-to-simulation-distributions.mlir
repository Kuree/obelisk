// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 Annex N gives every $dist_* function a separate explicit
// inout seed. Lowering passes that seed to one distribution op and stores the
// op's next_seed result, without reseeding the active process stream.
// Single-parameter distributions pad the second parameter with zero.

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-DAG: %[[FREEDOM:.*]] = arith.constant 3 : i32
// CHECK-DAG: %[[UNUSED:.*]] = arith.constant 0 : i32
// CHECK-NOT: obelisk_sim.random.seed
// CHECK: %[[NORMAL_RESULT:.*]], %[[NORMAL_SEED:.*]] = obelisk_sim.random.distribution {{.*}} {distribution = 1 : i32}
// CHECK: %[[NORMAL_SEED_LOGIC:.*]] = obelisk_sim.logic.from_bits %[[NORMAL_SEED]]
// CHECK: obelisk_sim.ref.store
// CHECK-NOT: obelisk_sim.random.seed
// CHECK: %[[CHI_RESULT:.*]], %[[CHI_SEED:.*]] = obelisk_sim.random.distribution {{.*}}, %[[FREEDOM]], %[[UNUSED]] {distribution = 4 : i32}
// CHECK: %[[CHI_SEED_LOGIC:.*]] = obelisk_sim.logic.from_bits %[[CHI_SEED]]
// CHECK: obelisk_sim.ref.store %[[CHI_SEED_LOGIC]]
// CHECK-NOT: obelisk_sim.random.seed
// CHECK: %[[ERLANG_RESULT:.*]], %[[ERLANG_SEED:.*]] = obelisk_sim.random.distribution {{.*}} {distribution = 6 : i32}
// CHECK-NOT: obelisk_sim.random.seed

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, original_source_range = !obelisk.source_range<"dist.sv", 1, 1, "dist.sv", 10, 10, "">, source_end_column = 10 : i64, source_end_line = 10 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 1, 1, "dist.sv", 10, 10, "">, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, original_source_range = !obelisk.source_range<"dist.sv", 1, 1, "dist.sv", 11, 1, "">, source_end_column = 1 : i64, source_end_line = 11 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 1, 1, "dist.sv", 11, 1, "">, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, original_source_range = !obelisk.source_range<"dist.sv", 1, 8, "dist.sv", 1, 8, "">, referenced_path = "top", referenced_symbol = @s0.top, source_end_column = 8 : i64, source_end_line = 1 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 1, 8, "dist.sv", 1, 8, "">, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, original_source_range = !obelisk.source_range<"dist.sv", 1, 1, "dist.sv", 10, 10, "">, source_end_column = 10 : i64, source_end_line = 10 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 1, 1, "dist.sv", 10, 10, "">, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.seed", lifetime = 1 : i32, name = "seed", node_id = 5 : i64, original_source_range = !obelisk.source_range<"dist.sv", 2, 9, "dist.sv", 2, 13, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 13 : i64, source_end_line = 2 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 2, 9, "dist.sv", 2, 13, "">, sym_name = "s5.seed"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.value", lifetime = 1 : i32, name = "value", node_id = 6 : i64, original_source_range = !obelisk.source_range<"dist.sv", 3, 9, "dist.sv", 3, 14, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 14 : i64, source_end_line = 3 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 3, 9, "dist.sv", 3, 14, "">, sym_name = "s6.value"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 7 : i64, original_source_range = !obelisk.source_range<"dist.sv", 4, 1, "dist.sv", 9, 4, "">, procedure_kind = 0 : i32, source_end_column = 4 : i64, source_end_line = 9 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 4, 1, "dist.sv", 9, 4, "">, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64, original_source_range = !obelisk.source_range<"dist.sv", 4, 9, "dist.sv", 9, 4, "">, source_end_column = 4 : i64, source_end_line = 9 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 4, 9, "dist.sv", 9, 4, "">} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64, original_source_range = !obelisk.source_range<"dist.sv", 4, 9, "dist.sv", 9, 4, "">, source_end_column = 4 : i64, source_end_line = 9 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 4, 9, "dist.sv", 9, 4, "">} {
              obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64, original_source_range = !obelisk.source_range<"dist.sv", 5, 2, "dist.sv", 5, 14, "">, source_end_column = 14 : i64, source_end_line = 5 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 5, 2, "dist.sv", 5, 14, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 11 : i64, original_source_range = !obelisk.source_range<"dist.sv", 5, 2, "dist.sv", 5, 13, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 13 : i64, source_end_line = 5 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 5, 2, "dist.sv", 5, 13, "">} {
                  obelisk.sv.expression.named_value attributes {node_id = 12 : i64, original_source_range = !obelisk.source_range<"dist.sv", 5, 2, "dist.sv", 5, 6, "">, referenced_path = "top.seed", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.seed, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 6 : i64, source_end_line = 5 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 5, 2, "dist.sv", 5, 6, "">} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 13 : i64, original_source_range = !obelisk.source_range<"dist.sv", 5, 9, "dist.sv", 5, 13, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 13 : i64, source_end_line = 5 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 5, 9, "dist.sv", 5, 13, "">} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1234", node_id = 14 : i64, original_source_range = !obelisk.source_range<"dist.sv", 5, 9, "dist.sv", 5, 13, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 13 : i64, source_end_line = 5 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 5, 9, "dist.sv", 5, 13, "">} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64, original_source_range = !obelisk.source_range<"dist.sv", 6, 2, "dist.sv", 6, 37, "">, source_end_column = 37 : i64, source_end_line = 6 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 6, 2, "dist.sv", 6, 37, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 16 : i64, original_source_range = !obelisk.source_range<"dist.sv", 6, 2, "dist.sv", 6, 36, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 36 : i64, source_end_line = 6 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 6, 2, "dist.sv", 6, 36, "">} {
                  obelisk.sv.expression.named_value attributes {node_id = 17 : i64, original_source_range = !obelisk.source_range<"dist.sv", 6, 2, "dist.sv", 6, 7, "">, referenced_path = "top.value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.value, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 7 : i64, source_end_line = 6 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 6, 2, "dist.sv", 6, 7, "">} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 18 : i64, original_source_range = !obelisk.source_range<"dist.sv", 6, 10, "dist.sv", 6, 36, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 36 : i64, source_end_line = 6 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 6, 10, "dist.sv", 6, 36, "">} {
                    obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$dist_normal", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 19 : i64, original_source_range = !obelisk.source_range<"dist.sv", 6, 10, "dist.sv", 6, 36, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 36 : i64, source_end_line = 6 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 6, 10, "dist.sv", 6, 36, "">, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 20 : i64, original_source_range = !obelisk.source_range<"dist.sv", 6, 23, "dist.sv", 6, 27, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 27 : i64, source_end_line = 6 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 6, 23, "dist.sv", 6, 27, "">} {
                        obelisk.sv.expression.named_value attributes {node_id = 21 : i64, original_source_range = !obelisk.source_range<"dist.sv", 6, 23, "dist.sv", 6, 27, "">, referenced_path = "top.seed", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.seed, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 27 : i64, source_end_line = 6 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 6, 23, "dist.sv", 6, 27, "">} {
                        }
                        obelisk.sv.expression.empty_argument attributes {node_id = 22 : i64, original_source_range = !obelisk.source_range<"dist.sv", 6, 23, "dist.sv", 6, 27, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 27 : i64, source_end_line = 6 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 6, 23, "dist.sv", 6, 27, "">} {
                        }
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 23 : i64, original_source_range = !obelisk.source_range<"dist.sv", 6, 29, "dist.sv", 6, 30, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 30 : i64, source_end_line = 6 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 6, 29, "dist.sv", 6, 30, "">} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "100", node_id = 24 : i64, original_source_range = !obelisk.source_range<"dist.sv", 6, 32, "dist.sv", 6, 35, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 35 : i64, source_end_line = 6 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 6, 32, "dist.sv", 6, 35, "">} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 25 : i64, original_source_range = !obelisk.source_range<"dist.sv", 7, 2, "dist.sv", 7, 36, "">, source_end_column = 36 : i64, source_end_line = 7 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 7, 2, "dist.sv", 7, 36, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 26 : i64, original_source_range = !obelisk.source_range<"dist.sv", 7, 2, "dist.sv", 7, 35, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 35 : i64, source_end_line = 7 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 7, 2, "dist.sv", 7, 35, "">} {
                  obelisk.sv.expression.named_value attributes {node_id = 27 : i64, original_source_range = !obelisk.source_range<"dist.sv", 7, 2, "dist.sv", 7, 7, "">, referenced_path = "top.value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.value, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 7 : i64, source_end_line = 7 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 7, 2, "dist.sv", 7, 7, "">} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 28 : i64, original_source_range = !obelisk.source_range<"dist.sv", 7, 10, "dist.sv", 7, 35, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 35 : i64, source_end_line = 7 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 7, 10, "dist.sv", 7, 35, "">} {
                    obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$dist_chi_square", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 29 : i64, original_source_range = !obelisk.source_range<"dist.sv", 7, 10, "dist.sv", 7, 35, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 35 : i64, source_end_line = 7 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 7, 10, "dist.sv", 7, 35, "">, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 30 : i64, original_source_range = !obelisk.source_range<"dist.sv", 7, 27, "dist.sv", 7, 31, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 31 : i64, source_end_line = 7 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 7, 27, "dist.sv", 7, 31, "">} {
                        obelisk.sv.expression.named_value attributes {node_id = 31 : i64, original_source_range = !obelisk.source_range<"dist.sv", 7, 27, "dist.sv", 7, 31, "">, referenced_path = "top.seed", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.seed, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 31 : i64, source_end_line = 7 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 7, 27, "dist.sv", 7, 31, "">} {
                        }
                        obelisk.sv.expression.empty_argument attributes {node_id = 32 : i64, original_source_range = !obelisk.source_range<"dist.sv", 7, 27, "dist.sv", 7, 31, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 31 : i64, source_end_line = 7 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 7, 27, "dist.sv", 7, 31, "">} {
                        }
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 33 : i64, original_source_range = !obelisk.source_range<"dist.sv", 7, 33, "dist.sv", 7, 34, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 34 : i64, source_end_line = 7 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 7, 33, "dist.sv", 7, 34, "">} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 34 : i64, original_source_range = !obelisk.source_range<"dist.sv", 8, 2, "dist.sv", 8, 37, "">, source_end_column = 37 : i64, source_end_line = 8 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 8, 2, "dist.sv", 8, 37, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 35 : i64, original_source_range = !obelisk.source_range<"dist.sv", 8, 2, "dist.sv", 8, 36, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 36 : i64, source_end_line = 8 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 8, 2, "dist.sv", 8, 36, "">} {
                  obelisk.sv.expression.named_value attributes {node_id = 36 : i64, original_source_range = !obelisk.source_range<"dist.sv", 8, 2, "dist.sv", 8, 7, "">, referenced_path = "top.value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.value, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 7 : i64, source_end_line = 8 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 8, 2, "dist.sv", 8, 7, "">} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 37 : i64, original_source_range = !obelisk.source_range<"dist.sv", 8, 10, "dist.sv", 8, 36, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 36 : i64, source_end_line = 8 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 8, 10, "dist.sv", 8, 36, "">} {
                    obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$dist_erlang", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 38 : i64, original_source_range = !obelisk.source_range<"dist.sv", 8, 10, "dist.sv", 8, 36, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 36 : i64, source_end_line = 8 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 8, 10, "dist.sv", 8, 36, "">, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 39 : i64, original_source_range = !obelisk.source_range<"dist.sv", 8, 23, "dist.sv", 8, 27, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 27 : i64, source_end_line = 8 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 8, 23, "dist.sv", 8, 27, "">} {
                        obelisk.sv.expression.named_value attributes {node_id = 40 : i64, original_source_range = !obelisk.source_range<"dist.sv", 8, 23, "dist.sv", 8, 27, "">, referenced_path = "top.seed", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.seed, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 27 : i64, source_end_line = 8 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 8, 23, "dist.sv", 8, 27, "">} {
                        }
                        obelisk.sv.expression.empty_argument attributes {node_id = 41 : i64, original_source_range = !obelisk.source_range<"dist.sv", 8, 23, "dist.sv", 8, 27, "">, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, source_end_column = 27 : i64, source_end_line = 8 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 8, 23, "dist.sv", 8, 27, "">} {
                        }
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 42 : i64, original_source_range = !obelisk.source_range<"dist.sv", 8, 29, "dist.sv", 8, 30, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 30 : i64, source_end_line = 8 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 8, 29, "dist.sv", 8, 30, "">} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "100", node_id = 43 : i64, original_source_range = !obelisk.source_range<"dist.sv", 8, 32, "dist.sv", 8, 35, "">, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_end_column = 35 : i64, source_end_line = 8 : i64, source_file = "dist.sv", source_range = !obelisk.source_range<"dist.sv", 8, 32, "dist.sv", 8, 35, "">} {
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
}
