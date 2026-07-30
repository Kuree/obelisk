// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "termination", name = "termination", node_id = 0 : i64, sym_name = "s0.termination"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "termination", is_uninstantiated = false, name = "termination", node_id = 3 : i64, referenced_path = "termination", referenced_symbol = @s0.termination, sym_name = "s3.termination"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "termination", name = "termination", node_id = 4 : i64, sym_name = "s4.termination"} {
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "termination.nested_finish", name = "nested_finish", node_id = 5 : i64, return_variable_path = "termination.nested_finish.nested_finish", return_variable_symbol = @s1.$root::@s3.termination::@s4.termination::@s5.nested_finish::@s6.nested_finish, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s5.nested_finish", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 6 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 7 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$finish", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 8 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.termination", system_scope_path = "termination.nested_finish", system_scope_symbol = @s1.$root::@s3.termination::@s4.termination::@s5.nested_finish} {
              }
            }
            obelisk.sv.statement.return attributes {node_id = 9 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 10 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "termination.nested_finish.nested_finish", is_compiler_generated, name = "nested_finish", node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s6.nested_finish"} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "termination", node_id = 12 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$finish", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 14 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.termination", system_scope_path = "termination", system_scope_symbol = @s1.$root::@s3.termination::@s4.termination} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "termination", node_id = 16 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 17 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$stop", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 18 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.termination", system_scope_path = "termination", system_scope_symbol = @s1.$root::@s3.termination::@s4.termination} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "termination", node_id = 20 : i64, procedure_kind = 0 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 21 : i64} {
            obelisk.sv.statement.list attributes {node_id = 22 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 24 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.termination", system_scope_path = "termination", system_scope_symbol = @s1.$root::@s3.termination::@s4.termination} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "info=%0d", node_id = 25 : i64, semantic_type = !obelisk.ranged_packed_array<63 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 26 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 27 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$warning", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 28 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.termination", system_scope_path = "termination", system_scope_symbol = @s1.$root::@s3.termination::@s4.termination} {
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$error", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 30 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.termination", system_scope_path = "termination", system_scope_symbol = @s1.$root::@s3.termination::@s4.termination} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "continued", node_id = 31 : i64, semantic_type = !obelisk.ranged_packed_array<71 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "termination", node_id = 32 : i64, procedure_kind = 0 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 33 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$fatal", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 34 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.termination", system_scope_path = "termination", system_scope_symbol = @s1.$root::@s3.termination::@s4.termination} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 35 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.string_literal attributes {constant_value = "fatal=%0d", node_id = 36 : i64, semantic_type = !obelisk.ranged_packed_array<71 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 37 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "termination", node_id = 38 : i64, sym_name = "s11"} {
          obelisk.sv.symbol.variable attributes {hierarchical_name = "termination.value", lifetime = 1 : i32, name = "value", node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s12.value"} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "termination", node_id = 40 : i64, procedure_kind = 0 : i32, sym_name = "s13", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 41 : i64} {
            obelisk.sv.statement.list attributes {node_id = 42 : i64} {
              obelisk.sv.statement.variable_declaration attributes {node_id = 43 : i64, referenced_path = "termination.value", referenced_symbol = @s1.$root::@s3.termination::@s4.termination::@s11::@s12.value} {
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 44 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 45 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {node_id = 46 : i64, referenced_path = "termination.value", referenced_symbol = @s1.$root::@s3.termination::@s4.termination::@s11::@s12.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "nested_finish", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 47 : i64, referenced_path = "termination.nested_finish", referenced_symbol = @s1.$root::@s3.termination::@s4.termination::@s5.nested_finish, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 48 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 49 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.termination", system_scope_path = "termination", system_scope_symbol = @s1.$root::@s3.termination::@s4.termination::@s11} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "after=%0d", node_id = 50 : i64, semantic_type = !obelisk.ranged_packed_array<71 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 51 : i64, referenced_path = "termination.value", referenced_symbol = @s1.$root::@s3.termination::@s4.termination::@s11::@s12.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// CHECK: obelisk_sim.finish
// CHECK-NEXT: obelisk_sim.return
// CHECK: obelisk_sim.stop
// CHECK-NEXT: obelisk_sim.return
// CHECK-DAG: obelisk_sim.bytes.constant "INFO:
// CHECK-DAG: obelisk_sim.bytes.constant "WARNING:
// CHECK-DAG: obelisk_sim.bytes.constant "ERROR:
// CHECK: obelisk_sim.bytes.constant "FATAL:
// CHECK: obelisk_sim.fatal
// CHECK-NEXT: obelisk_sim.display
// CHECK-NEXT: obelisk_sim.return
// CHECK: obelisk_sim.termination.requested
// CHECK: cf.cond_br
// CHECK-NOT: obelisk.sv.
