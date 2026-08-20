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
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.seed", lifetime = 1 : i32, name = "seed", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s5.seed"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.value", lifetime = 1 : i32, name = "value", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s6.value"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "top.seed", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.seed, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1234", node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "top.value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.value, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$dist_normal", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "top.seed", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.seed, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        }
                        obelisk.sv.expression.empty_argument attributes {node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        }
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "100", node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 25 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 26 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  obelisk.sv.expression.named_value attributes {node_id = 27 : i64, referenced_path = "top.value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.value, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 28 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$dist_chi_square", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 29 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 30 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_path = "top.seed", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.seed, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        }
                        obelisk.sv.expression.empty_argument attributes {node_id = 32 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        }
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 33 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 34 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 35 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  obelisk.sv.expression.named_value attributes {node_id = 36 : i64, referenced_path = "top.value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.value, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 37 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$dist_erlang", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 38 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "top.seed", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.seed, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        }
                        obelisk.sv.expression.empty_argument attributes {node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        }
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 42 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "100", node_id = 43 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
