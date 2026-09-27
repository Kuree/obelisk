// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// Runtime behavior is checked in ../Runtime/obelisk-to-simulation-std-randomize.test.

// REQUIRES: z3

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "unit"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @top, sym_name = "instance"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "body", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.x", lifetime = 1 : i32, name = "x", node_id = 55 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "design_x"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.y", lifetime = 1 : i32, name = "y", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "y"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.limit", lifetime = 1 : i32, name = "limit", node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "limit"} {
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top", node_id = 29 : i64, sym_name = "scope"} {
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.x", lifetime = 0 : i32, name = "x", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "x"} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 8 : i64, procedure_kind = 0 : i32, sym_name = "initial", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 30 : i64} {
            obelisk.sv.statement.list attributes {node_id = 31 : i64} {
              obelisk.sv.statement.variable_declaration attributes {node_id = 32 : i64, referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@scope::@x} {
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 46 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 47 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 48 : i64, referenced_path = "top.limit", referenced_symbol = @root::@instance::@body::@limit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "9", is_signed = true, node_id = 49 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 10 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @root::@instance::@body::@scope} {
              obelisk.sv.constraint.list attributes {item_count = 3 : i64, node_id = 11 : i64} {
                obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 12 : i64} {
                  obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 13 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 14 : i64, referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@scope::@x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 15 : i64, referenced_path = "top.limit", referenced_symbol = @root::@instance::@body::@limit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
                obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 16 : i64} {
                  obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 17 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 18 : i64, referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_signed = true, node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
                obelisk.sv.constraint.solve_before attributes {after_count = 1 : i64, node_id = 20 : i64, solve_count = 1 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 21 : i64, referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@scope::@x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 22 : i64, referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 24 : i64, referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@scope::@x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.empty_argument attributes {is_signed = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 26 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 27 : i64, referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.empty_argument attributes {is_signed = true, node_id = 28 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 50 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 51 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @root::@instance::@body::@scope} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "solve %0d %0d %0d", node_id = 52 : i64, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 53 : i64, referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@scope::@x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 54 : i64, referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 56 : i64, referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@design_x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 33 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 34 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 35 : i64, referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 36 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @root::@instance::@body::@scope} {
                    obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 37 : i64} {
                      obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 38 : i64} {
                        obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 39 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                          obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 40 : i64, referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          }
                          obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 41 : i64, referenced_path = "top.limit", referenced_symbol = @root::@instance::@body::@limit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          }
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 42 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 43 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @root::@instance::@body::@scope} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "checker %0d", node_id = 44 : i64, semantic_type = !obelisk.ranged_packed_array<87 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 45 : i64, referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// std::randomize reads state-only variables through the ordinary capture
// inventory, writes only the listed variables, and owns the caller process's
// stream. The native and bytecode RUN lines validate both executable paths.
// CHECK-LABEL: simulation.func private @unit_0
// CHECK: [[STATE:%[^, ]+]], [[INCREMENT:%[^ ]+]] = simulation.random.state [[CONTEXT:%[^ ]+]]
// CHECK: [[CAPTURE:%[^ ]+]] = simulation.ref.load %{{.*}} : !simulation.ref<i32> -> i32
// CHECK: [[CAPTURE_BITS:%[^ ]+]] = arith.extui [[CAPTURE]] : i32 to i64
// CHECK: simulation.random.set_state [[CONTEXT]], %{{.*}}, [[INCREMENT]]
// CHECK: [[SAMPLE_LOW:%[^ ]+]] = arith.andi %{{.*}}, %{{.*}} : i64
// CHECK: [[CAPTURE_LOW:%[^ ]+]] = arith.andi [[CAPTURE_BITS]], %{{.*}} : i64
// CHECK: [[ZERO_HIGH:%[^ ]+]] = arith.andi [[SAMPLE_LOW]], %{{.*}} : i64
// CHECK: [[CONSTRAINED:%[^ ]+]] = arith.ori [[ZERO_HIGH]], [[CAPTURE_LOW]] : i64
// CHECK: simulation.ref.store %{{.*}} to %{{.*}} : i32, !simulation.ref<i32>
// CHECK-NOT: simulation.random.state
// CHECK-NOT: simulation.random.set_state
// CHECK: simulation.display
// CHECK: %[[CHECKER_LEFT:.*]] = simulation.ref.load
// CHECK: %[[CHECKER_RIGHT:.*]] = simulation.ref.load
// CHECK: arith.cmpi eq, %[[CHECKER_LEFT]], %[[CHECKER_RIGHT]] : i32
// CHECK: simulation.ref.store
// CHECK-NOT: simulation.managed.load
// CHECK-NOT: simulation.managed.store
