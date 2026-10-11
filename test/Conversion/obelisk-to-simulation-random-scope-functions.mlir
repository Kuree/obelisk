// RUN: %split-file %s %t
// RUN: not obelisk-opt %t/output.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=OUTPUT
// RUN: not obelisk-opt %t/cycle.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=CYCLE

// IEEE 1800-2023 18.5.11 applies the same function restrictions to scope calls.
// OUTPUT: error: constraint functions cannot have output, inout, or non-const ref arguments
// CYCLE: error: solve before ordering contains a cycle

//--- output.mlir
module {
  obelisk.sv.symbol.definition @top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64} {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 2 : i64} {
    obelisk.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", referenced_path = "top", referenced_symbol = @top, node_id = 3 : i64} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 4 : i64} {
        obelisk.sv.symbol.variable @x attributes {hierarchical_name = "top.x", name = "x", lifetime = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 6 : i64} {}
        }
        obelisk.sv.symbol.variable @y attributes {hierarchical_name = "top.y", name = "y", lifetime = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 8 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 9 : i64} {}
        }
        obelisk.sv.symbol.subroutine @id attributes {hierarchical_name = "top.id", name = "id", return_variable_path = "top.id.id", return_variable_symbol = @root::@instance::@body::@id::@result, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.integral<1, false, false, 0 : 0, bit>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 11 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 12 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 13 : i64} {
              obelisk.sv.expression.named_value attributes {referenced_path = "top.id.id", referenced_symbol = @root::@instance::@body::@id::@result, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 14 : i64} {}
              obelisk.sv.expression.named_value attributes {referenced_path = "top.id.value", referenced_symbol = @root::@instance::@body::@id::@value, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 15 : i64} {}
            }
          }
          obelisk.sv.symbol.formal_argument @value attributes {direction = 1 : i32, hierarchical_name = "top.id.value", name = "value", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18 : i64} {}
          obelisk.sv.symbol.variable @result attributes {hierarchical_name = "top.id.id", is_compiler_generated, name = "id", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 19 : i64} {}
        }
        obelisk.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 21 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 22 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 23 : i64} {
              obelisk.sv.constraint.list attributes {item_count = 2 : i64, node_id = 24 : i64} {
                obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 25 : i64} {
                  obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 26 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 27 : i64} {}
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "id", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.id", referenced_symbol = @root::@instance::@body::@id, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, node_id = 28 : i64} {
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 29 : i64} {}
                    }
                  }
                }
                obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 33 : i64} {
                  obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 34 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 35 : i64} {}
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 36 : i64} {}
                  }
                }
              }
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 40 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 41 : i64} {}
                obelisk.sv.expression.empty_argument attributes {semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 42 : i64} {}
              }
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 44 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 45 : i64} {}
                obelisk.sv.expression.empty_argument attributes {semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 46 : i64} {}
              }
            }
          }
        }
      }
    }
  }
}

//--- cycle.mlir
module {
  obelisk.sv.symbol.definition @top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64} {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 2 : i64} {
    obelisk.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", referenced_path = "top", referenced_symbol = @top, node_id = 3 : i64} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 4 : i64} {
        obelisk.sv.symbol.variable @x attributes {hierarchical_name = "top.x", name = "x", lifetime = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 6 : i64} {}
        }
        obelisk.sv.symbol.variable @y attributes {hierarchical_name = "top.y", name = "y", lifetime = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 8 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 9 : i64} {}
        }
        obelisk.sv.symbol.subroutine @id attributes {hierarchical_name = "top.id", name = "id", return_variable_path = "top.id.id", return_variable_symbol = @root::@instance::@body::@id::@result, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.integral<1, false, false, 0 : 0, bit>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 11 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 12 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 13 : i64} {
              obelisk.sv.expression.named_value attributes {referenced_path = "top.id.id", referenced_symbol = @root::@instance::@body::@id::@result, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 14 : i64} {}
              obelisk.sv.expression.named_value attributes {referenced_path = "top.id.value", referenced_symbol = @root::@instance::@body::@id::@value, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 15 : i64} {}
            }
          }
          obelisk.sv.symbol.formal_argument @value attributes {direction = 0 : i32, hierarchical_name = "top.id.value", name = "value", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18 : i64} {}
          obelisk.sv.symbol.variable @result attributes {hierarchical_name = "top.id.id", is_compiler_generated, name = "id", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 19 : i64} {}
        }
        obelisk.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 21 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 22 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 23 : i64} {
              obelisk.sv.constraint.list attributes {item_count = 2 : i64, node_id = 24 : i64} {
                obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 25 : i64} {
                  obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 26 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 27 : i64} {}
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "id", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.id", referenced_symbol = @root::@instance::@body::@id, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, node_id = 28 : i64} {
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 29 : i64} {}
                    }
                  }
                }
                obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 33 : i64} {
                  obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 34 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 35 : i64} {}
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "id", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.id", referenced_symbol = @root::@instance::@body::@id, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, node_id = 36 : i64} {
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 37 : i64} {}
                    }
                  }
                }
              }
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 42 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 43 : i64} {}
                obelisk.sv.expression.empty_argument attributes {semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 44 : i64} {}
              }
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 46 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 47 : i64} {}
                obelisk.sv.expression.empty_argument attributes {semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 48 : i64} {}
              }
            }
          }
        }
      }
    }
  }
}
