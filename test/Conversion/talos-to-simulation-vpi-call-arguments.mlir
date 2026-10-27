// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=0' --mlir-print-debuginfo -o %t
// RUN: env TALOS_TEST_INPUT=%t %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.CallArguments
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=3' --mlir-print-debuginfo -o %t.opt
// RUN: env TALOS_TEST_INPUT=%t.opt %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.CallArguments
// RUN: talos-opt %t --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null
!word = !talos.integral<4, false, false, 3 : 0, bit>
!half = !talos.integral<2, false, false, 1 : 0, bit>
module attributes {llvm.data_layout = "e-p:64:64-i64:64", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
talos.sv.symbol.definition @definition attributes {node_id = 76 : i64, name = "top", hierarchical_name = "top", definition_kind = 0 : i32} {}
talos.sv.symbol.root @root attributes {node_id = 75 : i64, name = "$root", hierarchical_name = "$root"} {
  talos.sv.symbol.instance @top attributes {node_id = 74 : i64, name = "top", hierarchical_name = "top", referenced_symbol = @definition, referenced_path = "top", is_uninstantiated = false} {
    talos.sv.symbol.instance_body @body attributes {node_id = 73 : i64, name = "top", hierarchical_name = "top"} {
      talos.sv.symbol.variable @a attributes {node_id = 1 : i64, name = "a", hierarchical_name = "top.a", lifetime = 1 : i32, semantic_type = !word} {}
      talos.sv.symbol.parameter @P attributes {node_id = 2 : i64, name = "P", hierarchical_name = "top.P", constant_value = "9", semantic_type = !word} {}
      talos.sv.symbol.subroutine @consume attributes {node_id = 10 : i64, name = "consume", hierarchical_name = "top.consume", subroutine_kind = 1 : i32, semantic_type = !talos.subroutine<(!word, !word, !word, !word, !word, !word) -> (), true>} {
        talos.sv.statement.empty  attributes {node_id = 9 : i64} {}
        talos.sv.symbol.formal_argument @x0 attributes {node_id = 3 : i64, name = "x0", hierarchical_name = "top.consume.x0", direction = 0 : i32, lifetime = 0 : i32, semantic_type = !word} {}
        talos.sv.symbol.formal_argument @x1 attributes {node_id = 4 : i64, name = "x1", hierarchical_name = "top.consume.x1", direction = 0 : i32, lifetime = 0 : i32, semantic_type = !word} {}
        talos.sv.symbol.formal_argument @x2 attributes {node_id = 5 : i64, name = "x2", hierarchical_name = "top.consume.x2", direction = 0 : i32, lifetime = 0 : i32, semantic_type = !word} {}
        talos.sv.symbol.formal_argument @x3 attributes {node_id = 6 : i64, name = "x3", hierarchical_name = "top.consume.x3", direction = 0 : i32, lifetime = 0 : i32, semantic_type = !word} {}
        talos.sv.symbol.formal_argument @x4 attributes {node_id = 7 : i64, name = "x4", hierarchical_name = "top.consume.x4", direction = 0 : i32, lifetime = 0 : i32, semantic_type = !word} {}
        talos.sv.symbol.formal_argument @x5 attributes {node_id = 8 : i64, name = "x5", hierarchical_name = "top.consume.x5", direction = 0 : i32, lifetime = 0 : i32, semantic_type = !word} {}
      }
      talos.sv.symbol.subroutine @f attributes {node_id = 15 : i64, name = "f", hierarchical_name = "top.f", subroutine_kind = 0 : i32, semantic_type = !talos.subroutine<(!word, !word) -> !word, false>} {
        talos.sv.statement.return  attributes {node_id = 14 : i64} {
          talos.sv.expression.named_value  attributes {node_id = 13 : i64, referenced_symbol = @root::@top::@body::@f::@x0, referenced_path = "top.f.x0", semantic_type = !word} {}
        }
        talos.sv.symbol.formal_argument @x0 attributes {node_id = 11 : i64, name = "x0", hierarchical_name = "top.f.x0", direction = 0 : i32, lifetime = 0 : i32, semantic_type = !word} {}
        talos.sv.symbol.formal_argument @x1 attributes {node_id = 12 : i64, name = "x1", hierarchical_name = "top.f.x1", direction = 0 : i32, lifetime = 0 : i32, semantic_type = !word} {}
      }
      talos.sv.symbol.subroutine @sink attributes {node_id = 19 : i64, name = "sink", hierarchical_name = "top.sink", subroutine_kind = 1 : i32, semantic_type = !talos.subroutine<(!word, !word) -> (), true>} {
        talos.sv.statement.empty  attributes {node_id = 18 : i64} {}
        talos.sv.symbol.formal_argument @x0 attributes {node_id = 16 : i64, name = "x0", hierarchical_name = "top.sink.x0", direction = 0 : i32, lifetime = 0 : i32, semantic_type = !word} {}
        talos.sv.symbol.formal_argument @x1 attributes {node_id = 17 : i64, name = "x1", hierarchical_name = "top.sink.x1", direction = 0 : i32, lifetime = 0 : i32, semantic_type = !word} {}
      }
      talos.sv.symbol.subroutine @empty attributes {node_id = 21 : i64, name = "empty", hierarchical_name = "top.empty", subroutine_kind = 1 : i32, semantic_type = !talos.subroutine<() -> (), true>} {
        talos.sv.statement.empty  attributes {node_id = 20 : i64} {}
      }
      talos.sv.symbol.subroutine @output attributes {node_id = 24 : i64, name = "output", hierarchical_name = "top.output", subroutine_kind = 1 : i32, semantic_type = !talos.subroutine<(!word) -> (), true>} {
        talos.sv.statement.empty  attributes {node_id = 23 : i64} {}
        talos.sv.symbol.formal_argument @x0 attributes {node_id = 22 : i64, name = "x0", hierarchical_name = "top.output.x0", direction = 1 : i32, lifetime = 0 : i32, semantic_type = !word} {}
      }
      talos.sv.symbol.subroutine @one attributes {node_id = 27 : i64, name = "one", hierarchical_name = "top.one", subroutine_kind = 1 : i32, semantic_type = !talos.subroutine<(!word) -> (), true>} {
        talos.sv.statement.empty  attributes {node_id = 26 : i64} {}
        talos.sv.symbol.formal_argument @x0 attributes {node_id = 25 : i64, name = "x0", hierarchical_name = "top.one.x0", direction = 0 : i32, lifetime = 0 : i32, semantic_type = !word} {}
      }
      talos.sv.symbol.procedural_block @initial0 attributes {node_id = 58 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
        talos.sv.statement.expression_statement  attributes {node_id = 57 : i64} {
          talos.sv.expression.call  attributes {node_id = 36 : i64, argument_count = 6 : i64, callee_name = "consume", constraint_restrictions = [], defaulted_arguments = array<i64:0,0,0,0,0,0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, referenced_path = "top.consume", referenced_symbol = @root::@top::@body::@consume, semantic_type = !talos.void, subroutine_kind = 1 : i32} {
            talos.sv.expression.named_value  attributes {node_id = 28 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !word} {}
            talos.sv.expression.named_value  attributes {node_id = 29 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !word} {}
            talos.sv.expression.integer_literal  attributes {node_id = 30 : i64, constant_value = "1", is_signed = false, semantic_type = !word} {}
            talos.sv.expression.integer_literal  attributes {node_id = 31 : i64, constant_value = "1", is_signed = false, semantic_type = !word} {}
            talos.sv.expression.concatenation  attributes {node_id = 34 : i64, semantic_type = !word, is_signed = false} {
              talos.sv.expression.integer_literal  attributes {node_id = 32 : i64, constant_value = "1", is_signed = false, semantic_type = !half} {}
              talos.sv.expression.integer_literal  attributes {node_id = 33 : i64, constant_value = "2", is_signed = false, semantic_type = !half} {}
            }
            talos.sv.expression.named_value  attributes {node_id = 35 : i64, referenced_symbol = @root::@top::@body::@P, referenced_path = "top.P", semantic_type = !word} {}
          } loc("call-arguments.sv":101:1)
        } loc("call-arguments.sv":101:1)
      }
      talos.sv.symbol.procedural_block @initial1 attributes {node_id = 60 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
        talos.sv.statement.expression_statement  attributes {node_id = 59 : i64} {
          talos.sv.expression.call  attributes {node_id = 39 : i64, argument_count = 2 : i64, callee_name = "f", constraint_restrictions = [], defaulted_arguments = array<i64:0,0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, referenced_path = "top.f", referenced_symbol = @root::@top::@body::@f, semantic_type = !word, subroutine_kind = 0 : i32} {
            talos.sv.expression.named_value  attributes {node_id = 37 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !word} {}
            talos.sv.expression.integer_literal  attributes {node_id = 38 : i64, constant_value = "2", is_signed = false, semantic_type = !word} {}
          } loc("call-arguments.sv":102:1)
        } loc("call-arguments.sv":102:1)
      }
      talos.sv.symbol.procedural_block @initial2 attributes {node_id = 62 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
        talos.sv.statement.expression_statement  attributes {node_id = 61 : i64} {
          talos.sv.expression.call  attributes {node_id = 44 : i64, argument_count = 2 : i64, callee_name = "sink", constraint_restrictions = [], defaulted_arguments = array<i64:0,0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, referenced_path = "top.sink", referenced_symbol = @root::@top::@body::@sink, semantic_type = !talos.void, subroutine_kind = 1 : i32} {
            talos.sv.expression.named_value  attributes {node_id = 40 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !word} {}
            talos.sv.expression.call  attributes {node_id = 43 : i64, argument_count = 2 : i64, callee_name = "f", constraint_restrictions = [], defaulted_arguments = array<i64:0,0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, referenced_path = "top.f", referenced_symbol = @root::@top::@body::@f, semantic_type = !word, subroutine_kind = 0 : i32} {
              talos.sv.expression.named_value  attributes {node_id = 41 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !word} {}
              talos.sv.expression.integer_literal  attributes {node_id = 42 : i64, constant_value = "3", is_signed = false, semantic_type = !word} {}
            } loc("call-arguments.sv":203:1)
          } loc("call-arguments.sv":103:1)
        } loc("call-arguments.sv":103:1)
      }
      talos.sv.symbol.procedural_block @initial3 attributes {node_id = 64 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
        talos.sv.statement.expression_statement  attributes {node_id = 63 : i64} {
          talos.sv.expression.call  attributes {node_id = 48 : i64, argument_count = 2 : i64, callee_name = "sink", constraint_restrictions = [], defaulted_arguments = array<i64:0,0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, referenced_path = "top.sink", referenced_symbol = @root::@top::@body::@sink, semantic_type = !talos.void, subroutine_kind = 1 : i32} {
            talos.sv.expression.named_value  attributes {node_id = 45 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !word} {}
            talos.sv.expression.conversion  attributes {node_id = 47 : i64, is_implicit = false, semantic_type = !word} {
              talos.sv.expression.integer_literal  attributes {node_id = 46 : i64, constant_value = "4", is_signed = false, semantic_type = !word} {}
            }
          } loc("call-arguments.sv":104:1)
        } loc("call-arguments.sv":104:1)
      }
      talos.sv.symbol.procedural_block @initial4 attributes {node_id = 66 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
        talos.sv.statement.expression_statement  attributes {node_id = 65 : i64} {
          talos.sv.expression.call  attributes {node_id = 49 : i64, argument_count = 0 : i64, callee_name = "empty", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, referenced_path = "top.empty", referenced_symbol = @root::@top::@body::@empty, semantic_type = !talos.void, subroutine_kind = 1 : i32} {} loc("call-arguments.sv":105:1)
        } loc("call-arguments.sv":105:1)
      }
      talos.sv.symbol.procedural_block @initial5 attributes {node_id = 68 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
        talos.sv.statement.expression_statement  attributes {node_id = 67 : i64} {
          talos.sv.expression.call  attributes {node_id = 52 : i64, argument_count = 2 : i64, callee_name = "sink", constraint_restrictions = [], defaulted_arguments = array<i64:0,1>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, referenced_path = "top.sink", referenced_symbol = @root::@top::@body::@sink, semantic_type = !talos.void, subroutine_kind = 1 : i32} {
            talos.sv.expression.named_value  attributes {node_id = 50 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !word} {}
            talos.sv.expression.integer_literal  attributes {node_id = 51 : i64, constant_value = "7", is_signed = false, semantic_type = !word} {}
          } loc("call-arguments.sv":106:1)
        } loc("call-arguments.sv":106:1)
      }
      talos.sv.symbol.procedural_block @initial6 attributes {node_id = 70 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
        talos.sv.statement.expression_statement  attributes {node_id = 69 : i64} {
          talos.sv.expression.call  attributes {node_id = 54 : i64, argument_count = 1 : i64, callee_name = "output", constraint_restrictions = [], defaulted_arguments = array<i64:0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, referenced_path = "top.output", referenced_symbol = @root::@top::@body::@output, semantic_type = !talos.void, subroutine_kind = 1 : i32} {
            talos.sv.expression.named_value  attributes {node_id = 53 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !word} {}
          } loc("call-arguments.sv":107:1)
        } loc("call-arguments.sv":107:1)
      }
      talos.sv.symbol.procedural_block @initial7 attributes {node_id = 72 : i64, hierarchical_name = "top", procedure_kind = 0 : i32} {
        talos.sv.statement.expression_statement  attributes {node_id = 71 : i64} {
          talos.sv.expression.call  attributes {node_id = 56 : i64, argument_count = 1 : i64, callee_name = "one", constraint_restrictions = [], defaulted_arguments = array<i64:0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, referenced_path = "top.one", referenced_symbol = @root::@top::@body::@one, semantic_type = !talos.void, subroutine_kind = 1 : i32} {
            talos.sv.expression.named_value  attributes {node_id = 55 : i64, referenced_symbol = @root::@top::@body::@a, referenced_path = "top.a", semantic_type = !word} {}
          } loc("call-arguments.sv":108:1)
        } loc("call-arguments.sv":108:1)
      }
    }
  }
}
}
