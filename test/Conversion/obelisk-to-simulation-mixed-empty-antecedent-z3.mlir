// REQUIRES: z3
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=O0
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

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
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.x", name = "x", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.x"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.x", lifetime = 1 : i32, name = "x", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.x"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.a", name = "a", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s10.a"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.d", name = "d", node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s11.d"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.d", lifetime = 1 : i32, name = "d", node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s12.d"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 13 : i64, procedure_kind = 2 : i32, sym_name = "s13", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 14 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 15 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 16 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 18 : i64, operator_kind = 12 : i32} {
                obelisk.sv.assertion.binary attributes {node_id = 19 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.binary attributes {node_id = 20 : i64, operator_kind = 1 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 21 : i64, repetition_is_unbounded = false, repetition_kind = 0 : i32, repetition_max = 0 : i64, repetition_min = 0 : i64} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "top.x", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.x, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 23 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 25 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 26 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 27 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 28 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 30 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "pass", is_signed = false, node_id = 31 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 32 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 33 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "fail", is_signed = false, node_id = 34 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// The two duplicate nonempty `a` alternatives share the next-clock handoff
// class and normalize to one solver cube. The empty match is deliberately
// in a distinct current-clock class, so it survives as the second channel.
// The enabled Z3 pipeline validates the minimized groups without merging
// temporal identity; x is never sampled and a/d are each sampled once.
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.branching_antecedent_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_match_channels = 2 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_result_horizon = 2 : i64
// CHECK-SAME: obelisk_sim.mixed_empty_antecedent_current_tick_channels = 1 : i64
// CHECK-SAME: obelisk_sim.mixed_empty_antecedent_handoff_channels = 1 : i64
// CHECK-SAME: obelisk_sim.mixed_empty_antecedent_nonoverlap
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_alternatives_after = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_alternatives_before = 3 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_literals_after = 1 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_literals_before = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_solver = "z3"
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_solver_queries = {{[1-9][0-9]*}} : i64
// O0-LABEL: obelisk_sim.func private @unit_0(
// O0-COUNT-2: obelisk_sim.ref.alloc
// O0: obelisk_sim.spawn @unit_0.$concurrent_eos_branch.
// O0-NOT: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.assert.sampled_read {{%.*}} from %arg3
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.assert.sampled_read {{%.*}} from %arg4
// CHECK-NOT: obelisk_sim.assert.sampled_read
