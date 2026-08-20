// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

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
        obelisk.sv.symbol.sequence attributes {has_default_instance = true, hierarchical_name = "top.fm", name = "fm", node_id = 45 : i64, port_count = 0 : i64, port_paths = [], port_symbols = [], sym_name = "s45.fm"} {
        }

        // Cover-property uses strong sequence semantics. The aggregate goto
        // DFA already completes each source attempt on its earliest endpoint,
        // so a direct outer first_match needs no priority state.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 2 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 11 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 12 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.first_match attributes {match_item_count = 0 : i64, node_id = 15 : i64} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 16 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 17 : i64, repetition_is_unbounded = true, repetition_kind = 2 : i32, repetition_min = 2 : i64} {
                    obelisk.sv.expression.named_value attributes {node_id = 18 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 19 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 21 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 22 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }

        // The same equivalence applies to a persistent consequent. The
        // nonoverlapped implication retains its handoff and the assertion's
        // default weak EOS result.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 30 : i64, procedure_kind = 2 : i32, sym_name = "s30", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 31 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 32 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 33 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 35 : i64, operator_kind = 12 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 36 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 37 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.first_match attributes {match_item_count = 0 : i64, node_id = 38 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 39 : i64, repetition_is_unbounded = true, repetition_kind = 0 : i32, repetition_min = 2 : i64} {
                    obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 41 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 42 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 43 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 44 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }

        // A named sequence invocation whose expanded executable body starts
        // with first_match is equally direct. Followed-by keeps its false-LHS
        // failure while the finite nonconsecutive DFA owns later results.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 50 : i64, procedure_kind = 2 : i32, sym_name = "s50", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 51 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 52 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 53 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 54 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 55 : i64, operator_kind = 13 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 56 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 57 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 58 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.assertion_instance attributes {argument_count = 0 : i64, argument_formal_paths = [], argument_formal_symbols = [], argument_kinds = array<i64>, has_expanded_body = true, is_recursive_property = false, local_variable_count = 0 : i64, local_variable_has_initializer = array<i64>, local_variable_paths = [], local_variable_symbols = [], node_id = 59 : i64, referenced_path = "top.fm", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s45.fm, semantic_type = !obelisk.sequence} {
                    obelisk.sv.assertion.first_match attributes {match_item_count = 0 : i64, node_id = 60 : i64} {
                      obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 61 : i64} {
                        obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 62 : i64, repetition_is_unbounded = false, repetition_kind = 1 : i32, repetition_max = 2 : i64, repetition_min = 1 : i64} {
                          obelisk.sv.expression.named_value attributes {node_id = 63 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                        obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 64 : i64, repetition_is_unbounded = false} {
                          obelisk.sv.expression.named_value attributes {node_id = 65 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
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
            obelisk.sv.statement.expression_statement attributes {node_id = 68 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 69 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }
      }
    }
  }
}

// The standalone cover monitor uses the original four-state goto DFA. Its
// action-silent strong EOS failure needs no coordinator, and no finite
// first_match priority operation or state allocation survives.
// CHECK-NOT: @unit_0.$concurrent_eos
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.first_match_monitor
// CHECK-SAME: obelisk_sim.persistent_first_match_equivalence
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "goto"
// CHECK-SAME: obelisk_sim.persistent_repetition_min = 2 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 4 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_unbounded
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK-COUNT-2: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_0.fork.11.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_0.fork.11.0.0
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.first_match_priority
// CHECK-NOT: obelisk.sv.assertion

// The implication retains the handoff plus its two-state consecutive DFA.
// Both live callbacks and the weak EOS pass are still observable once each.
// CHECK-LABEL: obelisk_sim.func private @unit_1.$concurrent_eos_count.31.repetition_weak(
// CHECK-COUNT-3: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK-COUNT-1: obelisk_sim.spawn @unit_1.fork.31.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_1.fork.31.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.consequent_first_match_equivalence
// CHECK-SAME: obelisk_sim.first_match_monitor
// CHECK-SAME: obelisk_sim.persistent_repetition_implication
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "consecutive"
// CHECK-SAME: obelisk_sim.persistent_repetition_min = 2 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_nonoverlapped
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 2 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_unbounded
// CHECK-COUNT-2: obelisk_sim.ref.alloc %{{.*}} : i64 -> !obelisk_sim.ref<i64>
// CHECK-NOT: obelisk_sim.ref.alloc %{{.*}} : i64 -> !obelisk_sim.ref<i64>
// CHECK: obelisk_sim.ref.alloc %{{.*}} {obelisk_sim.persistent_implication_handoff}
// CHECK: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_1.fork.31.0.0
// CHECK: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_1.fork.31.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_1.fork.31.0.0
// CHECK-COUNT-1: obelisk_sim.spawn @unit_1.fork.31.1.1
// CHECK-NOT: obelisk_sim.spawn @unit_1.fork.31.1.1
// CHECK-NOT: obelisk_sim.first_match_priority
// CHECK-NOT: obelisk.sv.assertion

// The named finite-[=] followed-by case completes the recognition matrix. Its
// three DFA cells weak-complete at EOS, and the antecedent/terminal `a` shares
// one sampled read from the same Preponed snapshot.
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_eos_count.51.repetition_weak(
// CHECK-COUNT-3: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK-COUNT-1: obelisk_sim.spawn @unit_2.fork.51.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_2.fork.51.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.consequent_first_match_equivalence
// CHECK-SAME: obelisk_sim.first_match_monitor
// CHECK-SAME: obelisk_sim.followed_by_monitor
// CHECK-SAME: obelisk_sim.persistent_repetition_implication
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "nonconsecutive"
// CHECK-SAME: obelisk_sim.persistent_repetition_max = 2 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_min = 1 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 3 : i64
// CHECK-COUNT-3: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_2.fork.51.1.1
// CHECK: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.logic.compare case_eq
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_2.fork.51.0.0
// CHECK: obelisk_sim.spawn @unit_2.fork.51.1.1
// CHECK-NOT: obelisk_sim.spawn @unit_2.fork
// CHECK-NOT: obelisk_sim.first_match_priority
// CHECK-NOT: obelisk.sv.assertion
