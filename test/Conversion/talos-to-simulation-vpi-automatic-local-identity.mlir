// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full instrument-vpi-frames=true opt-level=0' -o %t.full0.mlir
// RUN: env TALOS_TEST_INPUT=%t.full0.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.AutomaticLocalIdentity
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=off opt-level=0' -o %t.off0.mlir
// RUN: env TALOS_TEST_INPUT=%t.off0.mlir TALOS_TEST_VPI_OFF=1 %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.AutomaticLocalIdentity
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full instrument-vpi-frames=true opt-level=3' -o %t.full3.mlir
// RUN: env TALOS_TEST_INPUT=%t.full3.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.AutomaticLocalIdentity
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=off opt-level=3' -o %t.off3.mlir
// RUN: env TALOS_TEST_INPUT=%t.off3.mlir TALOS_TEST_VPI_OFF=1 %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.AutomaticLocalIdentity

// Distinct unnamed scopes deliberately share the public path top.i.
// The escaped variable in $local also matches an otherwise plausible private key.
!word = !talos.integral<32, true, false, 31 : 0, int>
!bit = !talos.integral<1, false, false, 0 : 0, bit>
!array = !talos.ranged_unpacked_array<2 : 3 x !word>
module attributes {llvm.target_triple = "x86_64-unknown-linux-gnu", llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"} {
  talos.sv.symbol.definition @def attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 110 : i64} {}
  talos.sv.symbol.definition @prefix_def attributes {definition_kind = 0 : i32, hierarchical_name = "prefix", name = "prefix", node_id = 111 : i64} {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 109 : i64} {
    talos.sv.symbol.instance @top attributes {hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_path = "top", referenced_symbol = @def, node_id = 104 : i64} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", node_id = 103 : i64} {
        talos.sv.symbol.subroutine @probe attributes {dpi_c_identifier = "automatic_local_identity_probe", hierarchical_name = "top.probe", is_dpi_context, is_dpi_import, name = "probe", semantic_type = !talos.subroutine<() -> !word, false>, subroutine_kind = 0 : i32, node_id = 4 : i64} {}
        talos.sv.symbol.variable @a attributes {hierarchical_name = "top.a", name = "a", lifetime = 1 : i32, semantic_type = !array, node_id = 5 : i64} {}
        talos.sv.symbol.statement_block @first attributes {block_kind = 0 : i32, hierarchical_name = "top", name = "", node_id = 9 : i64} {
          talos.sv.symbol.variable @i attributes {hierarchical_name = "top.i", name = "i", lifetime = 0 : i32, semantic_type = !word, node_id = 8 : i64} {}
          talos.sv.symbol.statement_block @nested attributes {block_kind = 0 : i32, hierarchical_name = "top", name = "", node_id = 7 : i64} {
            talos.sv.symbol.variable @i attributes {hierarchical_name = "top.i", name = "i", lifetime = 0 : i32, semantic_type = !word, node_id = 6 : i64} {}
          }
        }
        talos.sv.symbol.statement_block @second attributes {block_kind = 0 : i32, hierarchical_name = "top", name = "", node_id = 11 : i64} {
          talos.sv.symbol.variable @i attributes {hierarchical_name = "top.i", name = "i", lifetime = 0 : i32, semantic_type = !word, node_id = 10 : i64} {}
        }
        talos.sv.symbol.statement_block @each1 attributes {block_kind = 0 : i32, hierarchical_name = "top", name = "", node_id = 13 : i64} {
          talos.sv.symbol.iterator @i attributes {array_type = !array, hierarchical_name = "top.i", index_method_name = "", is_const, name = "i", semantic_type = !word, node_id = 12 : i64} {}
        }
        talos.sv.symbol.statement_block @each2 attributes {block_kind = 0 : i32, hierarchical_name = "top", name = "", node_id = 15 : i64} {
          talos.sv.symbol.iterator @i attributes {array_type = !array, hierarchical_name = "top.i", index_method_name = "", is_const, name = "i", semantic_type = !word, node_id = 14 : i64} {}
        }
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, node_id = 102 : i64} {
          talos.sv.statement.block attributes {node_id = 101 : i64} {
            talos.sv.statement.list attributes {node_id = 100 : i64} {
              talos.sv.statement.block attributes {block_symbol = @root::@top::@body::@first, is_implicit_loop_scope, node_id = 47 : i64} {
                talos.sv.statement.list attributes {node_id = 46 : i64} {
                  talos.sv.statement.variable_declaration attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@first::@i, node_id = 39 : i64} {
                    talos.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !word, is_signed = true, node_id = 38 : i64} {}
                  }
                  talos.sv.statement.for_loop attributes {has_local_variables = true, has_condition = true, initializer_count = 0 : i64, step_count = 1 : i64, node_id = 45 : i64} {
                    talos.sv.expression.binary_op attributes {operator_kind = 16 : i32, semantic_type = !bit, is_signed = false, node_id = 42 : i64} {
                      talos.sv.expression.named_value attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@first::@i, semantic_type = !word, is_signed = true, node_id = 40 : i64} {}
                      talos.sv.expression.integer_literal attributes {constant_value = "2", semantic_type = !word, is_signed = true, node_id = 41 : i64} {}
                    }
                    talos.sv.expression.unary_op attributes {operator_kind = 12 : i32, semantic_type = !word, is_signed = true, node_id = 44 : i64} {
                      talos.sv.expression.named_value attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@first::@i, semantic_type = !word, is_signed = true, node_id = 43 : i64} {}
                    }
                    talos.sv.statement.block attributes {block_symbol = @root::@top::@body::@first::@nested, is_implicit_loop_scope, node_id = 37 : i64} {
                      talos.sv.statement.list attributes {node_id = 36 : i64} {
                        talos.sv.statement.variable_declaration attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@first::@nested::@i, node_id = 29 : i64} {
                          talos.sv.expression.integer_literal attributes {constant_value = "10", semantic_type = !word, is_signed = true, node_id = 28 : i64} {}
                        }
                        talos.sv.statement.for_loop attributes {has_local_variables = true, has_condition = true, initializer_count = 0 : i64, step_count = 1 : i64, node_id = 35 : i64} {
                          talos.sv.expression.binary_op attributes {operator_kind = 16 : i32, semantic_type = !bit, is_signed = false, node_id = 32 : i64} {
                            talos.sv.expression.named_value attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@first::@nested::@i, semantic_type = !word, is_signed = true, node_id = 30 : i64} {}
                            talos.sv.expression.integer_literal attributes {constant_value = "12", semantic_type = !word, is_signed = true, node_id = 31 : i64} {}
                          }
                          talos.sv.expression.unary_op attributes {operator_kind = 12 : i32, semantic_type = !word, is_signed = true, node_id = 34 : i64} {
                            talos.sv.expression.named_value attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@first::@nested::@i, semantic_type = !word, is_signed = true, node_id = 33 : i64} {}
                          }
                          talos.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 3 : i64, condition_pattern_flags = array<i64: 0, 0, 0>, has_else = false, node_id = 27 : i64} {
                            talos.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !bit, is_signed = false, node_id = 18 : i64} {
                              talos.sv.expression.hierarchical_value attributes {referenced_path = "$local.@root::@top::@body::@first::@i", referenced_symbol = @root::@prefix::@body::@trap, semantic_type = !word, is_signed = true, node_id = 16 : i64} {}
                              talos.sv.expression.integer_literal attributes {constant_value = "77", semantic_type = !word, is_signed = true, node_id = 17 : i64} {}
                            }
                            talos.sv.expression.binary_op attributes {operator_kind = 13 : i32, semantic_type = !bit, is_signed = false, node_id = 21 : i64} {
                              talos.sv.expression.named_value attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@first::@nested::@i, semantic_type = !word, is_signed = true, node_id = 19 : i64} {}
                              talos.sv.expression.integer_literal attributes {constant_value = "10", semantic_type = !word, is_signed = true, node_id = 20 : i64} {}
                            }
                            talos.sv.expression.binary_op attributes {operator_kind = 16 : i32, semantic_type = !bit, is_signed = false, node_id = 24 : i64} {
                              talos.sv.expression.named_value attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@first::@nested::@i, semantic_type = !word, is_signed = true, node_id = 22 : i64} {}
                              talos.sv.expression.integer_literal attributes {constant_value = "12", semantic_type = !word, is_signed = true, node_id = 23 : i64} {}
                            }
                            talos.sv.statement.expression_statement attributes {node_id = 26 : i64} {
                              talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "probe", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.probe", referenced_symbol = @root::@top::@body::@probe, semantic_type = !word, subroutine_kind = 0 : i32, node_id = 25 : i64} {}
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
              talos.sv.statement.block attributes {block_symbol = @root::@top::@body::@second, is_implicit_loop_scope, node_id = 69 : i64} {
                talos.sv.statement.list attributes {node_id = 68 : i64} {
                  talos.sv.statement.variable_declaration attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@second::@i, node_id = 61 : i64} {
                    talos.sv.expression.integer_literal attributes {constant_value = "5", semantic_type = !word, is_signed = true, node_id = 60 : i64} {}
                  }
                  talos.sv.statement.for_loop attributes {has_local_variables = true, has_condition = true, initializer_count = 0 : i64, step_count = 1 : i64, node_id = 67 : i64} {
                    talos.sv.expression.binary_op attributes {operator_kind = 16 : i32, semantic_type = !bit, is_signed = false, node_id = 64 : i64} {
                      talos.sv.expression.named_value attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@second::@i, semantic_type = !word, is_signed = true, node_id = 62 : i64} {}
                      talos.sv.expression.integer_literal attributes {constant_value = "7", semantic_type = !word, is_signed = true, node_id = 63 : i64} {}
                    }
                    talos.sv.expression.unary_op attributes {operator_kind = 12 : i32, semantic_type = !word, is_signed = true, node_id = 66 : i64} {
                      talos.sv.expression.named_value attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@second::@i, semantic_type = !word, is_signed = true, node_id = 65 : i64} {}
                    }
                    talos.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 3 : i64, condition_pattern_flags = array<i64: 0, 0, 0>, has_else = false, node_id = 59 : i64} {
                      talos.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !bit, is_signed = false, node_id = 50 : i64} {
                        talos.sv.expression.hierarchical_value attributes {referenced_path = "$local.@root::@top::@body::@first::@i", referenced_symbol = @root::@prefix::@body::@trap, semantic_type = !word, is_signed = true, node_id = 48 : i64} {}
                        talos.sv.expression.integer_literal attributes {constant_value = "77", semantic_type = !word, is_signed = true, node_id = 49 : i64} {}
                      }
                      talos.sv.expression.binary_op attributes {operator_kind = 13 : i32, semantic_type = !bit, is_signed = false, node_id = 53 : i64} {
                        talos.sv.expression.named_value attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@second::@i, semantic_type = !word, is_signed = true, node_id = 51 : i64} {}
                        talos.sv.expression.integer_literal attributes {constant_value = "5", semantic_type = !word, is_signed = true, node_id = 52 : i64} {}
                      }
                      talos.sv.expression.binary_op attributes {operator_kind = 16 : i32, semantic_type = !bit, is_signed = false, node_id = 56 : i64} {
                        talos.sv.expression.named_value attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@second::@i, semantic_type = !word, is_signed = true, node_id = 54 : i64} {}
                        talos.sv.expression.integer_literal attributes {constant_value = "7", semantic_type = !word, is_signed = true, node_id = 55 : i64} {}
                      }
                      talos.sv.statement.expression_statement attributes {node_id = 58 : i64} {
                        talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "probe", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.probe", referenced_symbol = @root::@top::@body::@probe, semantic_type = !word, subroutine_kind = 0 : i32, node_id = 57 : i64} {}
                      }
                    }
                  }
                }
              }
              talos.sv.statement.block attributes {block_symbol = @root::@top::@body::@each1, is_implicit_loop_scope, node_id = 84 : i64} {
                talos.sv.statement.foreach_loop attributes {loop_dimensions = [{has_static_range = true, left = 2 : i64, right = 3 : i64, has_iterator = true, iterator_path = "top.i", iterator_symbol = @root::@top::@body::@each1::@i, iterator_type = !word}], node_id = 83 : i64} {
                  talos.sv.expression.named_value attributes {referenced_path = "top.a", referenced_symbol = @root::@top::@body::@a, semantic_type = !array, node_id = 70 : i64} {}
                  talos.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 3 : i64, condition_pattern_flags = array<i64: 0, 0, 0>, has_else = false, node_id = 82 : i64} {
                    talos.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !bit, is_signed = false, node_id = 73 : i64} {
                      talos.sv.expression.hierarchical_value attributes {referenced_path = "$local.@root::@top::@body::@first::@i", referenced_symbol = @root::@prefix::@body::@trap, semantic_type = !word, is_signed = true, node_id = 71 : i64} {}
                      talos.sv.expression.integer_literal attributes {constant_value = "77", semantic_type = !word, is_signed = true, node_id = 72 : i64} {}
                    }
                    talos.sv.expression.binary_op attributes {operator_kind = 13 : i32, semantic_type = !bit, is_signed = false, node_id = 76 : i64} {
                      talos.sv.expression.named_value attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@each1::@i, semantic_type = !word, is_signed = true, node_id = 74 : i64} {}
                      talos.sv.expression.integer_literal attributes {constant_value = "2", semantic_type = !word, is_signed = true, node_id = 75 : i64} {}
                    }
                    talos.sv.expression.binary_op attributes {operator_kind = 16 : i32, semantic_type = !bit, is_signed = false, node_id = 79 : i64} {
                      talos.sv.expression.named_value attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@each1::@i, semantic_type = !word, is_signed = true, node_id = 77 : i64} {}
                      talos.sv.expression.integer_literal attributes {constant_value = "4", semantic_type = !word, is_signed = true, node_id = 78 : i64} {}
                    }
                    talos.sv.statement.expression_statement attributes {node_id = 81 : i64} {
                      talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "probe", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.probe", referenced_symbol = @root::@top::@body::@probe, semantic_type = !word, subroutine_kind = 0 : i32, node_id = 80 : i64} {}
                    }
                  }
                }
              }
              talos.sv.statement.block attributes {block_symbol = @root::@top::@body::@each2, is_implicit_loop_scope, node_id = 99 : i64} {
                talos.sv.statement.foreach_loop attributes {loop_dimensions = [{has_static_range = true, left = 2 : i64, right = 3 : i64, has_iterator = true, iterator_path = "top.i", iterator_symbol = @root::@top::@body::@each2::@i, iterator_type = !word}], node_id = 98 : i64} {
                  talos.sv.expression.named_value attributes {referenced_path = "top.a", referenced_symbol = @root::@top::@body::@a, semantic_type = !array, node_id = 85 : i64} {}
                  talos.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 3 : i64, condition_pattern_flags = array<i64: 0, 0, 0>, has_else = false, node_id = 97 : i64} {
                    talos.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !bit, is_signed = false, node_id = 88 : i64} {
                      talos.sv.expression.hierarchical_value attributes {referenced_path = "$local.@root::@top::@body::@first::@i", referenced_symbol = @root::@prefix::@body::@trap, semantic_type = !word, is_signed = true, node_id = 86 : i64} {}
                      talos.sv.expression.integer_literal attributes {constant_value = "77", semantic_type = !word, is_signed = true, node_id = 87 : i64} {}
                    }
                    talos.sv.expression.binary_op attributes {operator_kind = 13 : i32, semantic_type = !bit, is_signed = false, node_id = 91 : i64} {
                      talos.sv.expression.named_value attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@each2::@i, semantic_type = !word, is_signed = true, node_id = 89 : i64} {}
                      talos.sv.expression.integer_literal attributes {constant_value = "2", semantic_type = !word, is_signed = true, node_id = 90 : i64} {}
                    }
                    talos.sv.expression.binary_op attributes {operator_kind = 16 : i32, semantic_type = !bit, is_signed = false, node_id = 94 : i64} {
                      talos.sv.expression.named_value attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@each2::@i, semantic_type = !word, is_signed = true, node_id = 92 : i64} {}
                      talos.sv.expression.integer_literal attributes {constant_value = "4", semantic_type = !word, is_signed = true, node_id = 93 : i64} {}
                    }
                    talos.sv.statement.expression_statement attributes {node_id = 96 : i64} {
                      talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "probe", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.probe", referenced_symbol = @root::@top::@body::@probe, semantic_type = !word, subroutine_kind = 0 : i32, node_id = 95 : i64} {}
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
    talos.sv.symbol.instance @prefix attributes {hierarchical_name = "$local", name = "$local", is_uninstantiated = false, referenced_path = "$local", referenced_symbol = @prefix_def, node_id = 108 : i64} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "$local", name = "$local", node_id = 107 : i64} {
        talos.sv.symbol.variable @trap attributes {hierarchical_name = "$local.@root::@top::@body::@first::@i", name = "@root::@top::@body::@first::@i", lifetime = 1 : i32, semantic_type = !word, node_id = 106 : i64} {
          talos.sv.expression.integer_literal attributes {constant_value = "77", semantic_type = !word, is_signed = true, node_id = 105 : i64} {}
        }
      }
    }
  }
}
