// REQUIRES: z3
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
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.c"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.d", lifetime = 1 : i32, name = "d", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.d"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.sel", lifetime = 1 : i32, name = "sel", node_id = 30 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s30.sel"} {
        }

        // One procedural evaluation starts on the subsequent positive edge.
        // Exact vacuity classes for (a implies b) iff (c implies d) initially
        // produce ten successful cubes. Z3 reduces their union to five cubes;
        // every true cube still completes this one expect evaluation only once.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 0 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 5 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 11 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 12 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 15 : i64, operator_kind = 5 : i32} {
                obelisk.sv.assertion.binary attributes {node_id = 16 : i64, operator_kind = 10 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 17 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 18 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 19 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 21 : i64, operator_kind = 10 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 22 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 24 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 26 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 27 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 28 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 29 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }

        // Negating a case property without a default produces one successful
        // cube: sel === 1'b1 && !a. Even though it is a singleton after exact
        // compilation, it must retain the four-state case guard.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 40 : i64, procedure_kind = 0 : i32, sym_name = "s40", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 5 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 41 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 42 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 43 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 44 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 45 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.case attributes {has_default = false, item_group_sizes = [1], node_id = 46 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 47 : i64, referenced_path = "top.sel", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s30.sel, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 48 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 49 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 50 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 51 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 52 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 53 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 54 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              }
            }
          }
        }
      }
    }
  }
}

// The one-shot actor samples each semantic predicate once, ORs all minimized
// cubes, and selects exactly one result store and completion trigger.
// CHECK: obelisk_sim.func private @[[MONITOR:[^(]+]](
// CHECK-SAME: domain = 0 : i32
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: obelisk_sim.expect_monitor_actor
// CHECK-SAME: obelisk_sim.expect_one_cycle_alternatives = 5 : i64
// CHECK-SAME: obelisk_sim.expect_one_cycle_branching
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_after = 5 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_before = 10 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_literals_after = 15 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_literals_before = 40 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_solver = "z3"
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK: %[[SUCCESS:.*]] = arith.constant {{.*}}true
// CHECK: obelisk_sim.ref.store %[[SUCCESS]]
// CHECK: obelisk_sim.ref.store {{.*}}true
// CHECK-NOT: obelisk_sim.ref.store
// CHECK: obelisk_sim.event.trigger
// CHECK-NOT: obelisk_sim.ref.store
// CHECK-NOT: obelisk_sim.event.trigger
// CHECK: %[[FAILURE:.*]] = arith.constant {{.*}}false
// CHECK: obelisk_sim.ref.store %[[FAILURE]]
// CHECK: obelisk_sim.ref.store {{.*}}true
// CHECK-NOT: obelisk_sim.ref.store
// CHECK: obelisk_sim.event.trigger
// CHECK-NOT: obelisk_sim.event.trigger
// The first sampled clock marks the evaluation started for EOS handling.
// CHECK: obelisk_sim.ref.store {{.*}}true
// CHECK-NOT: obelisk_sim.ref.store
// CHECK-COUNT-4: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: cf.cond_br
// CHECK-NOT: obelisk_sim.suspend.edge

// CHECK: obelisk_sim.func private @[[MONITOR]].$expect_eos.11(
// CHECK-SAME: obelisk_sim.expect_eos_coordinator
// CHECK-SAME: obelisk_sim.expect_operand_strength = "strong"
// CHECK: obelisk_sim.ref.load
// CHECK: cf.cond_br
// CHECK: arith.constant {{.*}}false
// CHECK: obelisk_sim.ref.store
// CHECK: arith.constant {{.*}}true
// CHECK: obelisk_sim.ref.store
// CHECK: obelisk_sim.event.trigger

// The procedural caller blocks on the monitor's private event, resumes in
// Reactive, and dispatches the selected pass/fail action exactly once.
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: domain = 0 : i32
// CHECK-SAME: home_region = 2 : i32
// CHECK: %[[DONE:.*]] = obelisk_sim.event.create
// CHECK: obelisk_sim.spawn @[[MONITOR]]
// CHECK: obelisk_sim.suspend.event %[[DONE]]
// CHECK-SAME: resume_region = 10 : i32
// CHECK: obelisk_sim.ref.load
// CHECK: cf.cond_br
// CHECK-NOT: obelisk.sv.assertion

// The singleton case complement stays on the one-cycle Boolean evaluator and
// therefore cannot lose its selector guard when branching collapses.
// CHECK: obelisk_sim.func private @[[CASE_MONITOR:unit_1[^ (]*]](
// CHECK-SAME: obelisk_sim.expect_monitor_actor
// CHECK-SAME: obelisk_sim.expect_one_cycle_alternatives = 1 : i64
// CHECK-NOT: obelisk_sim.expect_one_cycle_branching
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK-COUNT-2: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.logic.compare case_eq
// CHECK: cf.cond_br
