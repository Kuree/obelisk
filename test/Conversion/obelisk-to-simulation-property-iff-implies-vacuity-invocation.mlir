// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// The expanded body of p is the fallthrough-vacuous implication. Its unused
// property actual, `nexttime d`, remains semantic inventory after expansion
// and must neither make the one-cycle property temporal nor sample d.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.b"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.c"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.d", lifetime = 1 : i32, name = "d", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.d"} {
        }
        obelisk.sv.symbol.property attributes {has_default_instance = false, hierarchical_name = "top.p", name = "p", node_id = 10 : i64, port_count = 1 : i64, port_paths = ["top.p.ignored"], port_symbols = [@s1.$root::@s3.top::@s4.top::@s10.p::@s11.ignored], sym_name = "s10.p"} {
          obelisk.sv.symbol.assertion_port attributes {has_default_value = false, hierarchical_name = "top.p.ignored", is_local_variable = false, name = "ignored", node_id = 11 : i64, semantic_type = !obelisk.property, sym_name = "s11.ignored"} {
          }
        }
        obelisk.sv.symbol.property attributes {has_default_instance = false, hierarchical_name = "top.q", name = "q", node_id = 12 : i64, port_count = 1 : i64, port_paths = ["top.q.ignored"], port_symbols = [@s1.$root::@s3.top::@s4.top::@s12.q::@s13.ignored], sym_name = "s12.q"} {
          obelisk.sv.symbol.assertion_port attributes {has_default_value = false, hierarchical_name = "top.q.ignored", is_local_variable = false, name = "ignored", node_id = 13 : i64, semantic_type = !obelisk.property, sym_name = "s13.ignored"} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 20 : i64, procedure_kind = 2 : i32, sym_name = "s20", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 21 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 22 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 23 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 24 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 40 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 41 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 25 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.assertion_instance attributes {argument_count = 1 : i64, argument_formal_paths = ["top.p.ignored"], argument_formal_symbols = [@s1.$root::@s3.top::@s4.top::@s10.p::@s11.ignored], argument_kinds = array<i64: 1>, has_expanded_body = true, is_recursive_property = false, local_variable_count = 0 : i64, local_variable_has_initializer = array<i64>, local_variable_paths = [], local_variable_symbols = [], node_id = 26 : i64, referenced_path = "top.p", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.p, semantic_type = !obelisk.property} {
                  obelisk.sv.assertion.binary attributes {node_id = 27 : i64, operator_kind = 10 : i32} {
                    obelisk.sv.assertion.conditional attributes {has_else = false, node_id = 28 : i64} {
                      obelisk.sv.expression.named_value attributes {node_id = 29 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 30 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 32 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 33 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.assertion.unary attributes {has_range = false, node_id = 34 : i64, operator_kind = 1 : i32, range_is_unbounded = false} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 35 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 36 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 37 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 38 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }

        // Conversely, q's expanded body is the ordinary Boolean property a.
        // Its unused actual is the fallthrough-vacuous property and must not
        // turn the executable body into a branching or vacuous monitor.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 50 : i64, procedure_kind = 2 : i32, sym_name = "s50", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 51 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 52 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 53 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 54 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 55 : i64, repetition_is_unbounded = false} {
                obelisk.sv.expression.assertion_instance attributes {argument_count = 1 : i64, argument_formal_paths = ["top.q.ignored"], argument_formal_symbols = [@s1.$root::@s3.top::@s4.top::@s12.q::@s13.ignored], argument_kinds = array<i64: 1>, has_expanded_body = true, is_recursive_property = false, local_variable_count = 0 : i64, local_variable_has_initializer = array<i64>, local_variable_paths = [], local_variable_symbols = [], node_id = 56 : i64, referenced_path = "top.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.q, semantic_type = !obelisk.property} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 57 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 58 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.binary attributes {node_id = 59 : i64, operator_kind = 10 : i32} {
                    obelisk.sv.assertion.conditional attributes {has_else = false, node_id = 60 : i64} {
                      obelisk.sv.expression.named_value attributes {node_id = 61 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 62 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 63 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 64 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 65 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 66 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 67 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-NOT: @unit_0.$concurrent_eos
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.branching_sequence_monitor
// CHECK-SAME: obelisk_sim.vacuous_sequence_alternatives = {{[1-9][0-9]*}} : i64
// CHECK-COUNT-3: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK-COUNT-1: obelisk_sim.spawn @unit_0.fork.21.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_0.fork.21.0.0
// CHECK-NOT: @unit_0.$concurrent_eos
// CHECK-NOT: obelisk.sv.assertion

// Only q's expanded `a` body is executable; the retained iff/implies-like
// property inventory contributes no samples, branching, vacuity, or EOS state.
// CHECK-NOT: @unit_1.$concurrent_eos
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-NOT: obelisk_sim.branching_sequence_monitor
// CHECK-NOT: obelisk_sim.vacuous_sequence_alternatives
// CHECK-COUNT-1: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK-COUNT-1: obelisk_sim.spawn @unit_1.fork.51.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_1.fork.51.0.0
// CHECK-NOT: @unit_1.$concurrent_eos
// CHECK-NOT: obelisk.sv.assertion
