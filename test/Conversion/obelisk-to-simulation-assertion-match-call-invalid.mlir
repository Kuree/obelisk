// RUN: %split-file %s %t
// RUN: not obelisk-opt %t/managed.mlir '--lower-obelisk-to-sim=opt-level=0' -o /dev/null 2>&1 | FileCheck %s --check-prefix=MANAGED
// RUN: not obelisk-opt %t/side-effect.mlir '--lower-obelisk-to-sim=opt-level=0' -o /dev/null 2>&1 | FileCheck %s --check-prefix=SIDE-EFFECT

// MANAGED: error: sampled nonlocal assertion match-call arguments currently require every referenced storage leaf to have a fixed packed type
// SIDE-EFFECT: error: sampled nonlocal assertion match-call arguments must be side-effect-free value expressions

//--- managed.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.q", lifetime = 1 : i32, name = "q", node_id = 7 : i64, semantic_type = !obelisk.queue<!obelisk.integral<1, false, true, 0 : 0, logic>, 0>, sym_name = "s7.q"} {
        }
        obelisk.sv.symbol.sequence attributes {has_default_instance = true, hierarchical_name = "top.s", name = "s", node_id = 8 : i64, port_count = 0 : i64, port_paths = [], port_symbols = [], sym_name = "s8.s"} {
          obelisk.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 1 : i64, local_variable_has_initializer = array<i64: 0>, local_variable_paths = ["top.s.x"], local_variable_symbols = [@s1.$root::@s3.top::@s4.top::@s8.s::@s10.x], node_id = 50 : i64, referenced_path = "top.s", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.s, semantic_type = !obelisk.sequence} {
            obelisk.sv.assertion.sequence_with_match attributes {has_repetition = false, match_item_count = 1 : i64, node_id = 51 : i64, repetition_is_unbounded = false} {
              obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 52 : i64, repetition_is_unbounded = false} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 53 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 54 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top.s", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s8.s} {
                obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 55 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 56 : i64, referenced_path = "top.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.q, semantic_type = !obelisk.queue<!obelisk.integral<1, false, true, 0 : 0, logic>, 0>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 57 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.local_assertion_var attributes {hierarchical_name = "top.s.x", name = "x", node_id = 58 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s10.x"} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 9 : i64, procedure_kind = 2 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 10 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 11 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 12 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 14 : i64, repetition_is_unbounded = false} {
                obelisk.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 1 : i64, local_variable_has_initializer = array<i64: 0>, local_variable_paths = ["top.s.x"], local_variable_symbols = [@s1.$root::@s3.top::@s4.top::@s8.s::@s10.x], node_id = 15 : i64, referenced_path = "top.s", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.s, semantic_type = !obelisk.sequence} {
                  obelisk.sv.assertion.sequence_with_match attributes {has_repetition = false, match_item_count = 1 : i64, node_id = 16 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 17 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 19 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top.s", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s8.s} {
                      obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 20 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "top.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.q, semantic_type = !obelisk.queue<!obelisk.integral<1, false, true, 0 : 0, logic>, 0>} {
                        }
                        obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
}

//--- side-effect.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.a"} {
        }
        obelisk.sv.symbol.sequence attributes {has_default_instance = true, hierarchical_name = "top.s", name = "s", node_id = 7 : i64, port_count = 0 : i64, port_paths = [], port_symbols = [], sym_name = "s7.s"} {
          obelisk.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 1 : i64, local_variable_has_initializer = array<i64: 0>, local_variable_paths = ["top.s.x"], local_variable_symbols = [@s1.$root::@s3.top::@s4.top::@s7.s::@s9.x], node_id = 50 : i64, referenced_path = "top.s", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.s, semantic_type = !obelisk.sequence} {
            obelisk.sv.assertion.sequence_with_match attributes {has_repetition = false, match_item_count = 1 : i64, node_id = 51 : i64, repetition_is_unbounded = false} {
              obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 52 : i64, repetition_is_unbounded = false} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 53 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 54 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top.s", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s7.s} {
                obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 55 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 56 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.local_assertion_var attributes {hierarchical_name = "top.s.x", name = "x", node_id = 57 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.x"} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 8 : i64, procedure_kind = 2 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 9 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 10 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 11 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 13 : i64, repetition_is_unbounded = false} {
                obelisk.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 1 : i64, local_variable_has_initializer = array<i64: 0>, local_variable_paths = ["top.s.x"], local_variable_symbols = [@s1.$root::@s3.top::@s4.top::@s7.s::@s9.x], node_id = 14 : i64, referenced_path = "top.s", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.s, semantic_type = !obelisk.sequence} {
                  obelisk.sv.assertion.sequence_with_match attributes {has_repetition = false, match_item_count = 1 : i64, node_id = 15 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 16 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 18 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top.s", system_scope_symbol = @s1.$root::@s3.top::@s4.top::@s7.s} {
                      obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 19 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 20 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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
}
