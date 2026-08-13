// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' --verify-each --mlir-print-op-generic > /dev/null

// The method spelling is decoded once into ArrayMethod. Cover associative-only
// methods plus shared expression methods so both dispatch paths remain typed.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "assoc_method_enum_dispatch", name = "assoc_method_enum_dispatch", node_id = 0 : i64, sym_name = "s0.assoc_method_enum_dispatch"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "assoc_method_enum_dispatch", is_uninstantiated = false, name = "assoc_method_enum_dispatch", node_id = 3 : i64, referenced_path = "assoc_method_enum_dispatch", referenced_symbol = @s0.assoc_method_enum_dispatch, sym_name = "s3.assoc_method_enum_dispatch"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "assoc_method_enum_dispatch", name = "assoc_method_enum_dispatch", node_id = 4 : i64, sym_name = "s4.assoc_method_enum_dispatch", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "assoc_method_enum_dispatch.values", lifetime = 1 : i32, name = "values", node_id = 5 : i64, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, false>, sym_name = "s5.values"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "assoc_method_enum_dispatch.key", lifetime = 1 : i32, name = "key", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s6.key"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "assoc_method_enum_dispatch.result", lifetime = 1 : i32, name = "result", node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s7.result"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "assoc_method_enum_dispatch.found", lifetime = 1 : i32, name = "found", node_id = 8 : i64, semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>, sym_name = "s8.found"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "assoc_method_enum_dispatch", node_id = 9 : i64, procedure_kind = 0 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 10 : i64} {
            obelisk.sv.statement.list attributes {node_id = 11 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 12 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 14 : i64, referenced_path = "assoc_method_enum_dispatch.result", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "size", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.assoc_method_enum_dispatch", system_scope_path = "assoc_method_enum_dispatch", system_scope_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "assoc_method_enum_dispatch.values", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s5.values, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, false>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 17 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 19 : i64, referenced_path = "assoc_method_enum_dispatch.result", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "num", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.assoc_method_enum_dispatch", system_scope_path = "assoc_method_enum_dispatch", system_scope_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "assoc_method_enum_dispatch.values", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s5.values, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, false>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 22 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 24 : i64, referenced_path = "assoc_method_enum_dispatch.result", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "exists", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.assoc_method_enum_dispatch", system_scope_path = "assoc_method_enum_dispatch", system_scope_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 26 : i64, referenced_path = "assoc_method_enum_dispatch.values", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s5.values, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, false>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 27 : i64, referenced_path = "assoc_method_enum_dispatch.key", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s6.key, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 28 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 29 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 30 : i64, referenced_path = "assoc_method_enum_dispatch.result", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "first", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 31 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.assoc_method_enum_dispatch", system_scope_path = "assoc_method_enum_dispatch", system_scope_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 32 : i64, referenced_path = "assoc_method_enum_dispatch.values", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s5.values, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, false>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 33 : i64, referenced_path = "assoc_method_enum_dispatch.key", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s6.key, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 34 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 35 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 36 : i64, referenced_path = "assoc_method_enum_dispatch.result", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "last", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 37 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.assoc_method_enum_dispatch", system_scope_path = "assoc_method_enum_dispatch", system_scope_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 38 : i64, referenced_path = "assoc_method_enum_dispatch.values", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s5.values, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, false>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 39 : i64, referenced_path = "assoc_method_enum_dispatch.key", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s6.key, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 40 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 42 : i64, referenced_path = "assoc_method_enum_dispatch.result", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "next", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 43 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.assoc_method_enum_dispatch", system_scope_path = "assoc_method_enum_dispatch", system_scope_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 44 : i64, referenced_path = "assoc_method_enum_dispatch.values", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s5.values, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, false>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 45 : i64, referenced_path = "assoc_method_enum_dispatch.key", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s6.key, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 46 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 47 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 48 : i64, referenced_path = "assoc_method_enum_dispatch.result", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "prev", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 49 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.assoc_method_enum_dispatch", system_scope_path = "assoc_method_enum_dispatch", system_scope_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 50 : i64, referenced_path = "assoc_method_enum_dispatch.values", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s5.values, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, false>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 51 : i64, referenced_path = "assoc_method_enum_dispatch.key", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s6.key, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 52 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 53 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 54 : i64, referenced_path = "assoc_method_enum_dispatch.result", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s7.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "sum", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 55 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.assoc_method_enum_dispatch", system_scope_path = "assoc_method_enum_dispatch", system_scope_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 56 : i64, referenced_path = "assoc_method_enum_dispatch.values", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s5.values, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, false>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 57 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 58 : i64, semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 59 : i64, referenced_path = "assoc_method_enum_dispatch.found", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s8.found, semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "find", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = true, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, iterator_variable_path = "assoc_method_enum_dispatch.item", iterator_variable_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s10.item, node_id = 60 : i64, semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>, subroutine_kind = 0 : i32, system_library_cell = "work.assoc_method_enum_dispatch", system_scope_path = "assoc_method_enum_dispatch", system_scope_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch} {
                    obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 61 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 62 : i64, referenced_path = "assoc_method_enum_dispatch.item", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s10.item, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 63 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 64 : i64, referenced_path = "assoc_method_enum_dispatch.values", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s5.values, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, false>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 65 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "delete", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 66 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.assoc_method_enum_dispatch", system_scope_path = "assoc_method_enum_dispatch", system_scope_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 67 : i64, referenced_path = "assoc_method_enum_dispatch.values", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s5.values, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, false>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 68 : i64, referenced_path = "assoc_method_enum_dispatch.key", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s6.key, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 69 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "delete", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 70 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.assoc_method_enum_dispatch", system_scope_path = "assoc_method_enum_dispatch", system_scope_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 71 : i64, referenced_path = "assoc_method_enum_dispatch.values", referenced_symbol = @s1.$root::@s3.assoc_method_enum_dispatch::@s4.assoc_method_enum_dispatch::@s5.values, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, false>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.iterator attributes {array_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, false>, hierarchical_name = "assoc_method_enum_dispatch.item", index_method_name = "index", is_const, name = "item", node_id = 72 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s10.item"} {
        }
      }
    }
  }
}


// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: obelisk_sim.container.size
// CHECK: obelisk_sim.assoc.exists
// CHECK-COUNT-4: obelisk_sim.assoc.traverse
// CHECK: arith.addi
// CHECK: obelisk_sim.assoc.delete
// CHECK: obelisk_sim.container.delete
