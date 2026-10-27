// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full instrument-vpi-frames=true opt-level=0' -o %t.mlir
// RUN: env TALOS_TEST_INPUT=%t.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.ForDeclarationScopes
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full instrument-vpi-frames=true opt-level=3' -o %t.opt.mlir
// RUN: env TALOS_TEST_INPUT=%t.opt.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.ForDeclarationScopes

!word = !talos.integral<32, true, false, 31 : 0, int>
!bit = !talos.integral<1, false, false, 0 : 0, bit>
module attributes {llvm.target_triple = "x86_64-unknown-linux-gnu", llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"} {
  talos.sv.symbol.definition @def attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64} {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 2 : i64} {
    talos.sv.symbol.instance @top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @def} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64} {
        talos.sv.symbol.subroutine @probe attributes {dpi_c_identifier = "for_scope_probe", hierarchical_name = "top.probe", is_dpi_context, is_dpi_import, name = "probe", node_id = 5 : i64, semantic_type = !talos.subroutine<() -> !word, false>, subroutine_kind = 0 : i32} {}
        talos.sv.symbol.statement_block @named attributes {block_kind = 0 : i32, hierarchical_name = "top.sweep", name = "sweep", node_id = 7 : i64} {
          talos.sv.symbol.variable @i attributes {hierarchical_name = "top.sweep.i", name = "i", lifetime = 0 : i32, semantic_type = !word, node_id = 8 : i64} {}
        }
        talos.sv.symbol.statement_block @unnamed attributes {block_kind = 0 : i32, hierarchical_name = "top", name = "", node_id = 9 : i64} {
          talos.sv.symbol.variable @i attributes {hierarchical_name = "top.i", name = "i", lifetime = 0 : i32, semantic_type = !word, node_id = 10 : i64} {}
        }
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, node_id = 11 : i64} {
          talos.sv.statement.block attributes {node_id = 12 : i64} {
            talos.sv.statement.list attributes {node_id = 13 : i64} {
              talos.sv.statement.block attributes {block_symbol = @root::@top::@body::@named, is_implicit_loop_scope, node_id = 14 : i64} {
                talos.sv.statement.list attributes {node_id = 15 : i64} {
                  talos.sv.statement.variable_declaration attributes {referenced_path = "top.sweep.i", referenced_symbol = @root::@top::@body::@named::@i, node_id = 16 : i64} {
                    talos.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !word, is_signed = true, node_id = 17 : i64} {}
                  }
                  talos.sv.statement.for_loop attributes {has_local_variables = true, has_condition = true, initializer_count = 0 : i64, step_count = 1 : i64, node_id = 18 : i64} {
                    talos.sv.expression.binary_op attributes {operator_kind = 16 : i32, semantic_type = !bit, is_signed = false, node_id = 19 : i64} {
                      talos.sv.expression.named_value attributes {referenced_path = "top.sweep.i", referenced_symbol = @root::@top::@body::@named::@i, semantic_type = !word, is_signed = true, node_id = 20 : i64} {}
                      talos.sv.expression.integer_literal attributes {constant_value = "2", semantic_type = !word, is_signed = true, node_id = 21 : i64} {}
                    }
                    talos.sv.expression.unary_op attributes {operator_kind = 12 : i32, semantic_type = !word, is_signed = true, node_id = 22 : i64} {
                      talos.sv.expression.named_value attributes {referenced_path = "top.sweep.i", referenced_symbol = @root::@top::@body::@named::@i, semantic_type = !word, is_signed = true, node_id = 23 : i64} {}
                    }
                    talos.sv.statement.expression_statement attributes {node_id = 24 : i64} {
                      talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "probe", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.probe", referenced_symbol = @root::@top::@body::@probe, semantic_type = !word, subroutine_kind = 0 : i32, node_id = 25 : i64} {}
                    }
                  }
                }
              }
              talos.sv.statement.block attributes {block_symbol = @root::@top::@body::@unnamed, is_implicit_loop_scope, node_id = 26 : i64} {
                talos.sv.statement.list attributes {node_id = 27 : i64} {
                  talos.sv.statement.variable_declaration attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@unnamed::@i, node_id = 28 : i64} {
                    talos.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !word, is_signed = true, node_id = 29 : i64} {}
                  }
                  talos.sv.statement.for_loop attributes {has_local_variables = true, has_condition = true, initializer_count = 0 : i64, step_count = 1 : i64, node_id = 30 : i64} {
                    talos.sv.expression.binary_op attributes {operator_kind = 16 : i32, semantic_type = !bit, is_signed = false, node_id = 31 : i64} {
                      talos.sv.expression.named_value attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@unnamed::@i, semantic_type = !word, is_signed = true, node_id = 32 : i64} {}
                      talos.sv.expression.integer_literal attributes {constant_value = "2", semantic_type = !word, is_signed = true, node_id = 33 : i64} {}
                    }
                    talos.sv.expression.unary_op attributes {operator_kind = 12 : i32, semantic_type = !word, is_signed = true, node_id = 34 : i64} {
                      talos.sv.expression.named_value attributes {referenced_path = "top.i", referenced_symbol = @root::@top::@body::@unnamed::@i, semantic_type = !word, is_signed = true, node_id = 35 : i64} {}
                    }
                    talos.sv.statement.expression_statement attributes {node_id = 36 : i64} {
                      talos.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "probe", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.probe", referenced_symbol = @root::@top::@body::@probe, semantic_type = !word, subroutine_kind = 0 : i32, node_id = 37 : i64} {}
                    }
                  }
                }
              }
              talos.sv.statement.for_loop attributes {has_local_variables = false, has_condition = true, initializer_count = 0 : i64, step_count = 0 : i64, node_id = 38 : i64} {
                talos.sv.expression.integer_literal attributes {constant_value = "1'b0", semantic_type = !bit, is_signed = false, node_id = 39 : i64} {}
                talos.sv.statement.empty attributes {node_id = 40 : i64} {}
              }
            }
          }
        }
      }
    }
  }
}
