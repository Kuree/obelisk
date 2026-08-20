// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// REQUIRES: z3

// One-cycle concurrent action controls snapshot the action mask once at each
// assertion clock.  The overlapped implication distinguishes vacuous pass,
// nonvacuous pass, and failure.  Boolean branching remains solver-minimized
// before the action-class gates are applied.

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "action_control_onecycle", name = "action_control_onecycle", node_id = 0 : i64, sym_name = "s0.action_control_onecycle"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "action_control_onecycle", is_uninstantiated = false, name = "action_control_onecycle", node_id = 3 : i64, referenced_path = "action_control_onecycle", referenced_symbol = @s0.action_control_onecycle, sym_name = "s3.action_control_onecycle"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "action_control_onecycle", name = "action_control_onecycle", node_id = 4 : i64, sym_name = "s4.action_control_onecycle", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "action_control_onecycle.clk", name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "action_control_onecycle.clk", lifetime = 1 : i32, name = "clk", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.clk"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "action_control_onecycle.a", name = "a", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "action_control_onecycle.a", lifetime = 1 : i32, name = "a", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.a"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "action_control_onecycle.b", name = "b", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.b"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "action_control_onecycle.b", lifetime = 1 : i32, name = "b", node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s10.b"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "action_control_onecycle", node_id = 11 : i64, procedure_kind = 0 : i32, sym_name = "s11", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 12 : i64} {
            obelisk.sv.statement.list attributes {node_id = 13 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$assertpassoff", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 15 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.action_control_onecycle", system_scope_path = "action_control_onecycle", system_scope_symbol = @s1.$root::@s3.action_control_onecycle::@s4.action_control_onecycle} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 17 : i64, referenced_path = "action_control_onecycle.target", referenced_symbol = @s1.$root::@s3.action_control_onecycle::@s4.action_control_onecycle::@s12.target, semantic_type = !obelisk.void} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$assertpassoff", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 19 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.action_control_onecycle", system_scope_path = "action_control_onecycle", system_scope_symbol = @s1.$root::@s3.action_control_onecycle::@s4.action_control_onecycle} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 21 : i64, referenced_path = "action_control_onecycle.branch_p", referenced_symbol = @s1.$root::@s3.action_control_onecycle::@s4.action_control_onecycle::@s14.branch_p, semantic_type = !obelisk.void} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "action_control_onecycle.target", name = "target", node_id = 22 : i64, sym_name = "s12.target"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "action_control_onecycle", node_id = 23 : i64, procedure_kind = 2 : i32, sym_name = "s13", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "action_control_onecycle.target", block_symbol = @s1.$root::@s3.action_control_onecycle::@s4.action_control_onecycle::@s12.target, node_id = 24 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 25 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 26 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 27 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 28 : i64, referenced_path = "action_control_onecycle.clk", referenced_symbol = @s1.$root::@s3.action_control_onecycle::@s4.action_control_onecycle::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 29 : i64, operator_kind = 11 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 30 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 31 : i64, referenced_path = "action_control_onecycle.a", referenced_symbol = @s1.$root::@s3.action_control_onecycle::@s4.action_control_onecycle::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 32 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 33 : i64, referenced_path = "action_control_onecycle.b", referenced_symbol = @s1.$root::@s3.action_control_onecycle::@s4.action_control_onecycle::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 34 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 35 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.action_control_onecycle", system_scope_path = "action_control_onecycle.target", system_scope_symbol = @s1.$root::@s3.action_control_onecycle::@s4.action_control_onecycle::@s12.target} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "pass", is_signed = false, node_id = 36 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "action_control_onecycle.branch_p", name = "branch_p", node_id = 37 : i64, sym_name = "s14.branch_p"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "action_control_onecycle", node_id = 38 : i64, procedure_kind = 2 : i32, sym_name = "s15", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "action_control_onecycle.branch_p", block_symbol = @s1.$root::@s3.action_control_onecycle::@s4.action_control_onecycle::@s14.branch_p, node_id = 39 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 40 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 41 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 42 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 43 : i64, referenced_path = "action_control_onecycle.clk", referenced_symbol = @s1.$root::@s3.action_control_onecycle::@s4.action_control_onecycle::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 44 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 45 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 46 : i64, referenced_path = "action_control_onecycle.a", referenced_symbol = @s1.$root::@s3.action_control_onecycle::@s4.action_control_onecycle::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 47 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 48 : i64, referenced_path = "action_control_onecycle.b", referenced_symbol = @s1.$root::@s3.action_control_onecycle::@s4.action_control_onecycle::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 49 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 50 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.action_control_onecycle", system_scope_path = "action_control_onecycle.branch_p", system_scope_symbol = @s1.$root::@s3.action_control_onecycle::@s4.action_control_onecycle::@s14.branch_p} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "branch", is_signed = false, node_id = 51 : i64, semantic_type = !obelisk.ranged_packed_array<47 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
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

// CHECK-COUNT-2: obelisk_sim.assert.control {{.*}} action 7 assertion
// CHECK-NOT: obelisk_sim.assert.control

// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.assertion_target_id = [[IMPL_ID:[0-9]+]] : i64
// CHECK: [[IMPL_ACTION:%.*]] = obelisk_sim.assert.action_state {{.*}} assertion [[IMPL_ID]] {obelisk_sim.concurrent_attempt_action_state}
// CHECK: [[VAC_MASK:%.*]] = arith.constant {{.*}} 2 : i32
// CHECK: arith.andi [[IMPL_ACTION]], [[VAC_MASK]] : i32
// CHECK: arith.andi {{.*}} {obelisk_sim.concurrent_action_class = "vacuous-pass", obelisk_sim.concurrent_action_control} : i1
// CHECK: [[FAIL_MASK:%.*]] = arith.constant {{.*}} 4 : i32
// CHECK: arith.andi [[IMPL_ACTION]], [[FAIL_MASK]] : i32
// CHECK: arith.andi {{.*}} {obelisk_sim.concurrent_action_class = "fail", obelisk_sim.concurrent_action_control} : i1
// CHECK: [[PASS_MASK:%.*]] = arith.constant {{.*}} 1 : i32
// CHECK: arith.andi [[IMPL_ACTION]], [[PASS_MASK]] : i32
// CHECK: arith.andi {{.*}} {obelisk_sim.concurrent_action_class = "nonvacuous-pass", obelisk_sim.concurrent_action_control} : i1
// CHECK-NOT: obelisk_sim.assert.action_state

// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.assertion_target_id = [[BRANCH_ID:[0-9]+]] : i64
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_sequence_monitor
// CHECK-SAME: obelisk_sim.sva_boolean_solver = "z3"
// CHECK: [[BRANCH_ACTION:%.*]] = obelisk_sim.assert.action_state {{.*}} assertion [[BRANCH_ID]] {obelisk_sim.concurrent_attempt_action_state}
// CHECK: [[BRANCH_PASS:%.*]] = arith.constant {{.*}} 1 : i32
// CHECK: arith.andi [[BRANCH_ACTION]], [[BRANCH_PASS]] : i32
// CHECK: arith.andi {{.*}} {obelisk_sim.concurrent_action_class = "nonvacuous-pass", obelisk_sim.concurrent_action_control} : i1
// CHECK: [[BRANCH_FAIL:%.*]] = arith.constant {{.*}} 4 : i32
// CHECK: arith.andi [[BRANCH_ACTION]], [[BRANCH_FAIL]] : i32
// CHECK: arith.andi {{.*}} {obelisk_sim.concurrent_action_class = "fail", obelisk_sim.concurrent_action_control} : i1
// CHECK-NOT: obelisk_sim.assert.action_state
