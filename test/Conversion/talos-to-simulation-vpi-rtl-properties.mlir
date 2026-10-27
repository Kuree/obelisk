// RUN: talos-opt %s '--lower-talos-to-sim=vpi=read opt-level=0' -o %t.mlir
// RUN: env TALOS_TEST_INPUT=%t.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.RTLStatementProperties
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=3' -o %t.opt.mlir
// RUN: env TALOS_TEST_INPUT=%t.opt.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.RTLStatementProperties
// Normal = and <= share AssignmentOp; compound operators retain their source
// operation even after execution lowering expands them into load/compute/store.
!word = !talos.integral<32, true, false, 31 : 0, int>
module attributes {llvm.target_triple = "x86_64-unknown-linux-gnu", llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"} {
  talos.sv.symbol.definition @def attributes {node_id = 107 : i64, definition_kind = 0 : i32, hierarchical_name = "top", name = "top"} {
  }
  talos.sv.symbol.root @root attributes {node_id = 111 : i64, hierarchical_name = "$root", name = "$root"} {
    talos.sv.symbol.compilation_unit @cu attributes {node_id = 108 : i64, hierarchical_name = "$unit"} {
    }
    talos.sv.symbol.instance @top attributes {node_id = 110 : i64, hierarchical_name = "top", name = "top", referenced_symbol = @def, referenced_path = "top", is_uninstantiated = false} {
      talos.sv.symbol.instance_body @body attributes {node_id = 109 : i64, hierarchical_name = "top", name = "top"} {
        talos.sv.symbol.variable @q attributes {node_id = 106 : i64, hierarchical_name = "top.q", name = "q", lifetime = 1 : i32, semantic_type = !word} {
        }
        talos.sv.symbol.procedural_block @initial attributes {node_id = 105 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
          talos.sv.statement.list  attributes {node_id = 104 : i64} {
            talos.sv.statement.expression_statement  attributes {node_id = 14 : i64} {
              talos.sv.expression.assignment  attributes {node_id = 13 : i64, assignment_kind = 0 : i32, semantic_type = !word} {
                talos.sv.expression.named_value  attributes {node_id = 11 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
                }
                talos.sv.expression.integer_literal  attributes {node_id = 12 : i64, constant_value = "1", semantic_type = !word} {
                }
              }
            }
            talos.sv.statement.expression_statement  attributes {node_id = 18 : i64} {
              talos.sv.expression.assignment  attributes {node_id = 17 : i64, assignment_kind = 1 : i32, semantic_type = !word} {
                talos.sv.expression.named_value  attributes {node_id = 15 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
                }
                talos.sv.expression.integer_literal  attributes {node_id = 16 : i64, constant_value = "1", semantic_type = !word} {
                }
              }
            }
            talos.sv.statement.expression_statement  attributes {node_id = 22 : i64} {
              talos.sv.expression.assignment  attributes {node_id = 21 : i64, assignment_kind = 0 : i32, semantic_type = !word, operator_kind = 0 : i32} {
                talos.sv.expression.named_value  attributes {node_id = 19 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
                }
                talos.sv.expression.integer_literal  attributes {node_id = 20 : i64, constant_value = "1", semantic_type = !word} {
                }
              }
            }
            talos.sv.statement.expression_statement  attributes {node_id = 26 : i64} {
              talos.sv.expression.assignment  attributes {node_id = 25 : i64, assignment_kind = 0 : i32, semantic_type = !word, operator_kind = 1 : i32} {
                talos.sv.expression.named_value  attributes {node_id = 23 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
                }
                talos.sv.expression.integer_literal  attributes {node_id = 24 : i64, constant_value = "1", semantic_type = !word} {
                }
              }
            }
            talos.sv.statement.expression_statement  attributes {node_id = 30 : i64} {
              talos.sv.expression.assignment  attributes {node_id = 29 : i64, assignment_kind = 0 : i32, semantic_type = !word, operator_kind = 2 : i32} {
                talos.sv.expression.named_value  attributes {node_id = 27 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
                }
                talos.sv.expression.integer_literal  attributes {node_id = 28 : i64, constant_value = "1", semantic_type = !word} {
                }
              }
            }
            talos.sv.statement.expression_statement  attributes {node_id = 34 : i64} {
              talos.sv.expression.assignment  attributes {node_id = 33 : i64, assignment_kind = 0 : i32, semantic_type = !word, operator_kind = 3 : i32} {
                talos.sv.expression.named_value  attributes {node_id = 31 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
                }
                talos.sv.expression.integer_literal  attributes {node_id = 32 : i64, constant_value = "1", semantic_type = !word} {
                }
              }
            }
            talos.sv.statement.expression_statement  attributes {node_id = 38 : i64} {
              talos.sv.expression.assignment  attributes {node_id = 37 : i64, assignment_kind = 0 : i32, semantic_type = !word, operator_kind = 4 : i32} {
                talos.sv.expression.named_value  attributes {node_id = 35 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
                }
                talos.sv.expression.integer_literal  attributes {node_id = 36 : i64, constant_value = "1", semantic_type = !word} {
                }
              }
            }
            talos.sv.statement.expression_statement  attributes {node_id = 42 : i64} {
              talos.sv.expression.assignment  attributes {node_id = 41 : i64, assignment_kind = 0 : i32, semantic_type = !word, operator_kind = 5 : i32} {
                talos.sv.expression.named_value  attributes {node_id = 39 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
                }
                talos.sv.expression.integer_literal  attributes {node_id = 40 : i64, constant_value = "1", semantic_type = !word} {
                }
              }
            }
            talos.sv.statement.expression_statement  attributes {node_id = 46 : i64} {
              talos.sv.expression.assignment  attributes {node_id = 45 : i64, assignment_kind = 0 : i32, semantic_type = !word, operator_kind = 6 : i32} {
                talos.sv.expression.named_value  attributes {node_id = 43 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
                }
                talos.sv.expression.integer_literal  attributes {node_id = 44 : i64, constant_value = "1", semantic_type = !word} {
                }
              }
            }
            talos.sv.statement.expression_statement  attributes {node_id = 50 : i64} {
              talos.sv.expression.assignment  attributes {node_id = 49 : i64, assignment_kind = 0 : i32, semantic_type = !word, operator_kind = 7 : i32} {
                talos.sv.expression.named_value  attributes {node_id = 47 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
                }
                talos.sv.expression.integer_literal  attributes {node_id = 48 : i64, constant_value = "1", semantic_type = !word} {
                }
              }
            }
            talos.sv.statement.expression_statement  attributes {node_id = 54 : i64} {
              talos.sv.expression.assignment  attributes {node_id = 53 : i64, assignment_kind = 0 : i32, semantic_type = !word, operator_kind = 23 : i32} {
                talos.sv.expression.named_value  attributes {node_id = 51 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
                }
                talos.sv.expression.integer_literal  attributes {node_id = 52 : i64, constant_value = "1", semantic_type = !word} {
                }
              }
            }
            talos.sv.statement.expression_statement  attributes {node_id = 58 : i64} {
              talos.sv.expression.assignment  attributes {node_id = 57 : i64, assignment_kind = 0 : i32, semantic_type = !word, operator_kind = 24 : i32} {
                talos.sv.expression.named_value  attributes {node_id = 55 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
                }
                talos.sv.expression.integer_literal  attributes {node_id = 56 : i64, constant_value = "1", semantic_type = !word} {
                }
              }
            }
            talos.sv.statement.expression_statement  attributes {node_id = 62 : i64} {
              talos.sv.expression.assignment  attributes {node_id = 61 : i64, assignment_kind = 0 : i32, semantic_type = !word, operator_kind = 25 : i32} {
                talos.sv.expression.named_value  attributes {node_id = 59 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
                }
                talos.sv.expression.integer_literal  attributes {node_id = 60 : i64, constant_value = "1", semantic_type = !word} {
                }
              }
            }
            talos.sv.statement.expression_statement  attributes {node_id = 66 : i64} {
              talos.sv.expression.assignment  attributes {node_id = 65 : i64, assignment_kind = 0 : i32, semantic_type = !word, operator_kind = 26 : i32} {
                talos.sv.expression.named_value  attributes {node_id = 63 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
                }
                talos.sv.expression.integer_literal  attributes {node_id = 64 : i64, constant_value = "1", semantic_type = !word} {
                }
              }
            }
            talos.sv.statement.case  attributes {node_id = 71 : i64, condition_kind = 0 : i32, check_kind = 0 : i32, item_count = 1 : i64, item_label_counts = array<i64: 1>, has_default = true} {
              talos.sv.expression.named_value  attributes {node_id = 67 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
              }
              talos.sv.expression.integer_literal  attributes {node_id = 68 : i64, constant_value = "1", semantic_type = !word} {
              }
              talos.sv.statement.empty  attributes {node_id = 69 : i64} {
              }
              talos.sv.statement.empty  attributes {node_id = 70 : i64} {
              }
            }
            talos.sv.statement.case  attributes {node_id = 76 : i64, condition_kind = 1 : i32, check_kind = 1 : i32, item_count = 1 : i64, item_label_counts = array<i64: 1>, has_default = true} {
              talos.sv.expression.named_value  attributes {node_id = 72 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
              }
              talos.sv.expression.integer_literal  attributes {node_id = 73 : i64, constant_value = "1", semantic_type = !word} {
              }
              talos.sv.statement.empty  attributes {node_id = 74 : i64} {
              }
              talos.sv.statement.empty  attributes {node_id = 75 : i64} {
              }
            }
            talos.sv.statement.case  attributes {node_id = 81 : i64, condition_kind = 2 : i32, check_kind = 3 : i32, item_count = 1 : i64, item_label_counts = array<i64: 1>, has_default = true} {
              talos.sv.expression.named_value  attributes {node_id = 77 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
              }
              talos.sv.expression.integer_literal  attributes {node_id = 78 : i64, constant_value = "1", semantic_type = !word} {
              }
              talos.sv.statement.empty  attributes {node_id = 79 : i64} {
              }
              talos.sv.statement.empty  attributes {node_id = 80 : i64} {
              }
            }
            talos.sv.statement.case  attributes {node_id = 86 : i64, condition_kind = 3 : i32, check_kind = 1 : i32, item_count = 1 : i64, item_label_counts = array<i64: 1>, has_default = true} {
              talos.sv.expression.named_value  attributes {node_id = 82 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
              }
              talos.sv.expression.integer_literal  attributes {node_id = 83 : i64, constant_value = "1", semantic_type = !word} {
              }
              talos.sv.statement.empty  attributes {node_id = 84 : i64} {
              }
              talos.sv.statement.empty  attributes {node_id = 85 : i64} {
              }
            }
            talos.sv.statement.case  attributes {node_id = 91 : i64, condition_kind = 0 : i32, check_kind = 2 : i32, item_count = 1 : i64, item_label_counts = array<i64: 1>, has_default = true} {
              talos.sv.expression.named_value  attributes {node_id = 87 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
              }
              talos.sv.expression.integer_literal  attributes {node_id = 88 : i64, constant_value = "1", semantic_type = !word} {
              }
              talos.sv.statement.empty  attributes {node_id = 89 : i64} {
              }
              talos.sv.statement.empty  attributes {node_id = 90 : i64} {
              }
            }
            talos.sv.statement.conditional  attributes {node_id = 94 : i64, check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false} {
              talos.sv.expression.named_value  attributes {node_id = 92 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
              }
              talos.sv.statement.empty  attributes {node_id = 93 : i64} {
              }
            }
            talos.sv.statement.conditional  attributes {node_id = 97 : i64, check_kind = 1 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false} {
              talos.sv.expression.named_value  attributes {node_id = 95 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
              }
              talos.sv.statement.empty  attributes {node_id = 96 : i64} {
              }
            }
            talos.sv.statement.conditional  attributes {node_id = 100 : i64, check_kind = 2 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false} {
              talos.sv.expression.named_value  attributes {node_id = 98 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
              }
              talos.sv.statement.empty  attributes {node_id = 99 : i64} {
              }
            }
            talos.sv.statement.conditional  attributes {node_id = 103 : i64, check_kind = 3 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false} {
              talos.sv.expression.named_value  attributes {node_id = 101 : i64, referenced_symbol = @root::@top::@body::@q, referenced_path = "top.q", semantic_type = !word} {
              }
              talos.sv.statement.empty  attributes {node_id = 102 : i64} {
              }
            }
          }
        }
      }
    }
  }
}
