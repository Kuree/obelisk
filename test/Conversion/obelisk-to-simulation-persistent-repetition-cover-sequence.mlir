// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// Plain persistent cover sequence retains each successful repetition thread
// so later legal endpoints are reported with source-attempt multiplicity.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.clk", name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.clk"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.a", name = "a", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.a"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.b", name = "b", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.b"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s10.b"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 11 : i64, procedure_kind = 2 : i32, sym_name = "s11", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 3 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 12 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 13 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 14 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 16 : i64, repetition_is_unbounded = true, repetition_kind = 0 : i32, repetition_min = 2 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 19 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 20 : i64, procedure_kind = 2 : i32, sym_name = "s12", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 3 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 21 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 22 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 23 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 25 : i64} {
                obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 26 : i64, repetition_is_unbounded = true, repetition_kind = 2 : i32, repetition_min = 2 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 27 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 28 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 29 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 30 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 31 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 32 : i64, procedure_kind = 2 : i32, sym_name = "s13", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 3 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 33 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 34 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 35 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 36 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 37 : i64} {
                obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 38 : i64, repetition_is_unbounded = true, repetition_kind = 1 : i32, repetition_min = 1 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 39 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 40 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 41 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 42 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 43 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 44 : i64, procedure_kind = 2 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 3 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 45 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 46 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 47 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 48 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 49 : i64, repetition_is_unbounded = false, repetition_kind = 2 : i32, repetition_max = 2 : i64, repetition_min = 1 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 50 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 54 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 55 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 56 : i64, procedure_kind = 2 : i32, sym_name = "s15", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 3 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 57 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 58 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 59 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 60 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 61 : i64, repetition_is_unbounded = false, repetition_kind = 1 : i32, repetition_max = 2 : i64, repetition_min = 1 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 62 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

// No-terminal consecutive repetition reports every qualifying true sample.
// The older eligible count is both the success count and part of next state;
// a newly started attempt joins it independently.
// CHECK-NOT: @unit_0.$concurrent_eos
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.persistent_repetition_all_matches
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "consecutive"
// CHECK-SAME: obelisk_sim.persistent_repetition_min = 2 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 2 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_unbounded
// CHECK-NOT: obelisk_sim.first_match_monitor
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.assert.sampled_read
// CHECK-NEXT: %[[U0_REPEAT:[[:alnum:]_]+]] = obelisk_sim.logic.is_true
// CHECK-NEXT: %[[U0_NEW:[[:alnum:]_]+]] = arith.extui %[[U0_REPEAT]] : i1 to i64
// CHECK-NEXT: %[[U0_ZERO:[[:alnum:]_]+]] = arith.constant {{.*}}0 : i64
// CHECK-NEXT: %[[U0_SUCCESS:[[:alnum:]_]+]] = arith.select %[[U0_REPEAT]], %[[U0_OLDER:[[:alnum:]_]+]], %[[U0_ZERO]] : i64
// CHECK-NEXT: %[[U0_NEXT:[[:alnum:]_]+]] = arith.addi %[[U0_NEW]], %[[U0_SUCCESS]] : i64
// CHECK-NEXT: cf.br ^{{.*}}(%[[U0_SUCCESS]] : i64)
// CHECK: cf.cond_br %{{.*}}, ^{{.*}}, ^{{.*}}(%[[U0_NEXT]] : i64)
// CHECK-COUNT-1: obelisk_sim.spawn @unit_0.fork.12.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_0.fork.12.0.0
// CHECK-NOT: obelisk.sv.assertion

// Unbounded goto retains a successful pending thread according to the current
// repeated term: true creates the next endpoint candidate and known false
// returns to the saturated wait state. Neither route is gated by terminal
// failure, so a terminal hit can report and continue on the same clock.
// CHECK-NOT: @unit_1.$concurrent_eos
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.persistent_repetition_all_matches
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "goto"
// CHECK-SAME: obelisk_sim.persistent_repetition_min = 2 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 4 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_unbounded
// CHECK-NOT: obelisk_sim.first_match_monitor
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.assert.sampled_read
// CHECK-NEXT: %[[U1_REPEAT:[[:alnum:]_]+]] = obelisk_sim.logic.is_true
// CHECK: %[[U1_FALSE:[[:alnum:]_]+]] = obelisk_sim.logic.compare case_eq
// CHECK: obelisk_sim.assert.sampled_read
// CHECK-NEXT: %[[U1_TERMINAL:[[:alnum:]_]+]] = obelisk_sim.logic.is_true
// CHECK: %[[U1_SUCCESS:[[:alnum:]_]+]] = arith.select %[[U1_TERMINAL]], %[[U1_PENDING:[[:alnum:]_]+]], %{{[[:alnum:]_]+}} : i64
// CHECK: %[[U1_KEEP_TRUE:[[:alnum:]_]+]] = arith.select %[[U1_REPEAT]], %[[U1_PENDING]], %{{[[:alnum:]_]+}} : i64
// CHECK: arith.addi %{{[[:alnum:]_]+}}, %[[U1_KEEP_TRUE]] : i64
// CHECK: %[[U1_KEEP_FALSE:[[:alnum:]_]+]] = arith.select %[[U1_FALSE]], %[[U1_PENDING]], %{{[[:alnum:]_]+}} : i64
// CHECK: arith.addi %{{[[:alnum:]_]+}}, %[[U1_KEEP_FALSE]] : i64
// CHECK: cf.br ^{{.*}}(%[[U1_SUCCESS]] : i64)
// CHECK-COUNT-1: obelisk_sim.spawn @unit_1.fork.21.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_1.fork.21.0.0
// CHECK-NOT: obelisk.sv.assertion

// Unbounded nonconsecutive repetition may report terminal on every eligible
// known-value clock. The same eligible amount remains in next state whether
// the repeated term is true or strictly false.
// CHECK-NOT: @unit_2.$concurrent_eos
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.persistent_repetition_all_matches
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "nonconsecutive"
// CHECK-SAME: obelisk_sim.persistent_repetition_min = 1 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 2 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_unbounded
// CHECK-NOT: obelisk_sim.first_match_monitor
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.assert.sampled_read
// CHECK-NEXT: %[[U2_REPEAT:[[:alnum:]_]+]] = obelisk_sim.logic.is_true
// CHECK: %[[U2_FALSE:[[:alnum:]_]+]] = obelisk_sim.logic.compare case_eq
// CHECK: obelisk_sim.assert.sampled_read
// CHECK-NEXT: %[[U2_TERMINAL:[[:alnum:]_]+]] = obelisk_sim.logic.is_true
// CHECK: %[[U2_SUCCESS:[[:alnum:]_]+]] = arith.select %[[U2_TERMINAL]], %[[U2_ELIGIBLE:[[:alnum:]_]+]], %{{[[:alnum:]_]+}} : i64
// CHECK-NEXT: %[[U2_KNOWN:[[:alnum:]_]+]] = arith.ori %[[U2_REPEAT]], %[[U2_FALSE]] : i1
// CHECK: %[[U2_RETAIN:[[:alnum:]_]+]] = arith.select %[[U2_KNOWN]], %[[U2_ELIGIBLE]], %{{[[:alnum:]_]+}} : i64
// CHECK: arith.addi %{{[[:alnum:]_]+}}, %[[U2_RETAIN]] : i64
// CHECK: cf.br ^{{.*}}(%[[U2_SUCCESS]] : i64)
// CHECK-COUNT-1: obelisk_sim.spawn @unit_2.fork.33.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_2.fork.33.0.0
// CHECK-NOT: obelisk.sv.assertion

// No-terminal finite goto reports the first and second occurrences, but only
// the first remains live. The second endpoint is absent from the next-state
// sum, so a third occurrence cannot be reported for that source attempt.
// CHECK-NOT: @unit_3.$concurrent_eos
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: obelisk_sim.persistent_repetition_all_matches
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "goto"
// CHECK-SAME: obelisk_sim.persistent_repetition_max = 2 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_min = 1 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 2 : i64
// CHECK-NOT: obelisk_sim.first_match_monitor
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.assert.sampled_read
// CHECK-NEXT: %[[U3_REPEAT:[[:alnum:]_]+]] = obelisk_sim.logic.is_true
// CHECK: %[[U3_FALSE:[[:alnum:]_]+]] = obelisk_sim.logic.compare case_eq
// CHECK: %[[U3_FIRST:[[:alnum:]_]+]] = arith.select %[[U3_REPEAT]], %[[U3_STARTS:[[:alnum:]_]+]], %{{[[:alnum:]_]+}} : i64
// CHECK: %[[U3_SECOND:[[:alnum:]_]+]] = arith.select %[[U3_REPEAT]], %[[U3_COUNT_ONE:[[:alnum:]_]+]], %{{[[:alnum:]_]+}} : i64
// CHECK-NEXT: %[[U3_SUCCESS:[[:alnum:]_]+]] = arith.addi %[[U3_FIRST]], %[[U3_SECOND]] : i64
// CHECK: %[[U3_WAIT:[[:alnum:]_]+]] = arith.select %[[U3_FALSE]], %[[U3_COUNT_ONE]], %{{[[:alnum:]_]+}} : i64
// CHECK-NEXT: %[[U3_NEXT:[[:alnum:]_]+]] = arith.addi %[[U3_FIRST]], %[[U3_WAIT]] : i64
// CHECK-NEXT: cf.br ^{{.*}}(%[[U3_SUCCESS]] : i64)
// CHECK: cf.cond_br %{{.*}}, ^{{.*}}, ^{{.*}}(%{{[[:alnum:]_]+}}, %[[U3_NEXT]] : i64, i64)
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-COUNT-1: obelisk_sim.spawn @unit_3.fork.45.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_3.fork.45.0.0
// CHECK-NOT: obelisk.sv.assertion

// No-terminal finite nonconsecutive repetition reports every true endpoint in
// range and every strictly-false trailing extension. At the maximum, only a
// strict false reports and retains the source; a further true terminates it.
// CHECK-NOT: @unit_4.$concurrent_eos
// CHECK-LABEL: obelisk_sim.func private @unit_4(
// CHECK-SAME: obelisk_sim.persistent_repetition_all_matches
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "nonconsecutive"
// CHECK-SAME: obelisk_sim.persistent_repetition_max = 2 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_min = 1 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 3 : i64
// CHECK-NOT: obelisk_sim.first_match_monitor
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.assert.sampled_read
// CHECK-NEXT: %[[U4_REPEAT:[[:alnum:]_]+]] = obelisk_sim.logic.is_true
// CHECK: %[[U4_FALSE:[[:alnum:]_]+]] = obelisk_sim.logic.compare case_eq
// CHECK: %[[U4_NEW:[[:alnum:]_]+]] = arith.select %[[U4_REPEAT]], %[[U4_STARTS:[[:alnum:]_]+]], %{{[[:alnum:]_]+}} : i64
// CHECK: %[[U4_TRUE_ONE:[[:alnum:]_]+]] = arith.select %[[U4_REPEAT]], %[[U4_COUNT_ONE:[[:alnum:]_]+]], %{{[[:alnum:]_]+}} : i64
// CHECK-NEXT: %[[U4_TRUE_ENDPOINTS:[[:alnum:]_]+]] = arith.addi %[[U4_NEW]], %[[U4_TRUE_ONE]] : i64
// CHECK: %[[U4_FALSE_ONE:[[:alnum:]_]+]] = arith.select %[[U4_FALSE]], %[[U4_COUNT_ONE]], %{{[[:alnum:]_]+}} : i64
// CHECK-NEXT: %[[U4_WITH_FALSE_ONE:[[:alnum:]_]+]] = arith.addi %[[U4_TRUE_ENDPOINTS]], %[[U4_FALSE_ONE]] : i64
// CHECK: %[[U4_NEXT_ONE:[[:alnum:]_]+]] = arith.addi %[[U4_NEW]], %[[U4_FALSE_ONE]] : i64
// CHECK: %[[U4_FALSE_MAX:[[:alnum:]_]+]] = arith.select %[[U4_FALSE]], %[[U4_COUNT_MAX:[[:alnum:]_]+]], %{{[[:alnum:]_]+}} : i64
// CHECK-NOT: arith.select %[[U4_REPEAT]], %[[U4_COUNT_MAX]]
// CHECK: %[[U4_ALL_ENDPOINTS:[[:alnum:]_]+]] = arith.addi %[[U4_WITH_FALSE_ONE]], %[[U4_FALSE_MAX]] : i64
// CHECK-NEXT: %[[U4_NEXT_MAX:[[:alnum:]_]+]] = arith.addi %[[U4_TRUE_ONE]], %[[U4_FALSE_MAX]] : i64
// CHECK-NEXT: cf.br ^{{.*}}(%[[U4_ALL_ENDPOINTS]] : i64)
// CHECK: cf.cond_br %{{.*}}, ^{{.*}}, ^{{.*}}(%{{[[:alnum:]_]+}}, %[[U4_NEXT_ONE]], %[[U4_NEXT_MAX]] : i64, i64, i64)
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-COUNT-1: obelisk_sim.spawn @unit_4.fork.57.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_4.fork.57.0.0
// CHECK-NOT: obelisk.sv.assertion
