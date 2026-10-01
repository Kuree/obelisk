// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.t attributes {definition_kind = 0 : i32, hierarchical_name = "t", name = "t", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.t attributes {hierarchical_name = "t", is_uninstantiated = false, name = "t", node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t} {
      obelisk.sv.symbol.instance_body @s4.t attributes {hierarchical_name = "t", name = "t", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s5.clk attributes {hierarchical_name = "t.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s6.a attributes {hierarchical_name = "t.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.clocking_block @s7.cb attributes {hierarchical_name = "t.cb", is_default = true, is_global = false, name = "cb", node_id = 7 : i64} {
          obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 8 : i64} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "t.clk", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
        }
        obelisk.sv.symbol.sequence @s8.s attributes {default_clocking_path = "t.cb", default_clocking_symbol = @s1.$root::@s3.t::@s4.t::@s7.cb, has_default_instance = true, hierarchical_name = "t.s", name = "s", node_id = 10 : i64, port_count = 0 : i64, port_paths = [], port_symbols = []} {
          obelisk.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 1 : i64, local_variable_has_initializer = array<i64: 0>, local_variable_paths = ["t.s.x"], local_variable_symbols = [@s1.$root::@s3.t::@s4.t::@s8.s::@s9.x], node_id = 11 : i64, referenced_path = "t.s", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s8.s, semantic_type = !obelisk.sequence} {
            obelisk.sv.assertion.sequence_with_match attributes {has_repetition = false, match_item_count = 2 : i64, node_id = 12 : i64, repetition_is_unbounded = false} {
              obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 13 : i64, repetition_is_unbounded = false} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "t.a", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 16 : i64, referenced_path = "t.s.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s8.s::@s9.x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.conversion attributes {is_signed = true, node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "t.a", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 19 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.t", system_scope_path = "t.s", system_scope_symbol = @s1.$root::@s3.t::@s4.t::@s8.s} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 20 : i64, referenced_path = "t.s.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s8.s::@s9.x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.local_assertion_var @s9.x attributes {hierarchical_name = "t.s.x", name = "x", node_id = 21 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
          obelisk.sv.symbol.local_assertion_var @s11.x attributes {hierarchical_name = "t.s.x", name = "x", node_id = 22 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.procedural_block @s10 attributes {hierarchical_name = "t", node_id = 23 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, default_clocking_path = "t.cb", default_clocking_symbol = @s1.$root::@s3.t::@s4.t::@s7.cb, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 24 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 25 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 26 : i64, referenced_path = "t.clk", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
            obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 27 : i64, repetition_is_unbounded = false, repetition_kind = 0 : i32, repetition_max = 2 : i64, repetition_min = 2 : i64} {
              obelisk.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, is_signed = false, local_variable_count = 1 : i64, local_variable_has_initializer = array<i64: 0>, local_variable_paths = ["t.s.x"], local_variable_symbols = [@s1.$root::@s3.t::@s4.t::@s8.s::@s11.x], node_id = 28 : i64, referenced_path = "t.s", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s8.s, semantic_type = !obelisk.sequence} {
                obelisk.sv.assertion.sequence_with_match attributes {has_repetition = false, match_item_count = 2 : i64, node_id = 29 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 30 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 31 : i64, referenced_path = "t.a", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 32 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 33 : i64, referenced_path = "t.s.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s8.s::@s11.x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.conversion attributes {is_signed = true, node_id = 34 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 35 : i64, referenced_path = "t.a", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 36 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.t", system_scope_path = "t.s", system_scope_symbol = @s1.$root::@s3.t::@s4.t::@s8.s} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 37 : i64, referenced_path = "t.s.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s8.s::@s11.x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 38 : i64} {
            }
          }
        }
      }
    }
  }
}

// The same semantic match-call node is copied into both fixed-repetition ages.
// Each occurrence gets a distinct callback/code-unit identity.
// CHECK-COUNT-2: simulation.concurrent_match_call
