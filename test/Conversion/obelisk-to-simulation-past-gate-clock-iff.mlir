// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// A $past gate and an explicit clock iff are independent sampled controls.
// A gate-only plan and an iff-only plan using the same signal share a sampler;
// adding a distinct gate to the qualified clock requires a second sampler.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.main_clk", lifetime = 1 : i32, name = "main_clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.main_clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.alternate_clk", lifetime = 1 : i32, name = "alternate_clk", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.alternate_clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.data", lifetime = 1 : i32, name = "data", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.data"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.gate", lifetime = 1 : i32, name = "gate", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.gate"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.qualify", lifetime = 1 : i32, name = "qualify", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.qualify"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 2 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          // A gate-only plan and an iff-only plan using the same signal are
          // semantically identical and must share one sampler.
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 24 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 25 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 26 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 27 : i64, referenced_path = "top.main_clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.main_clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 28 : i64, repetition_is_unbounded = false} {
                obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$past", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 29 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 30 : i64, referenced_path = "top.data", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.data, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 31 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 32 : i64, referenced_path = "top.qualify", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.qualify, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.clocking_event attributes {is_signed = false, node_id = 33 : i64, semantic_type = !obelisk.void} {
                    obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 34 : i64} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 35 : i64, referenced_path = "top.alternate_clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.alternate_clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 49 : i64, procedure_kind = 2 : i32, sym_name = "s11", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 36 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 37 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 38 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 39 : i64, referenced_path = "top.main_clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.main_clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 40 : i64, repetition_is_unbounded = false} {
                obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$past", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 1, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 41 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 42 : i64, referenced_path = "top.data", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.data, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 43 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 44 : i64, semantic_type = !obelisk.void} {
                  }
                  obelisk.sv.expression.clocking_event attributes {is_signed = false, node_id = 45 : i64, semantic_type = !obelisk.void} {
                    obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = true, node_id = 46 : i64} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 47 : i64, referenced_path = "top.alternate_clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.alternate_clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 48 : i64, referenced_path = "top.qualify", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.qualify, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 50 : i64, procedure_kind = 2 : i32, sym_name = "s12", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 11 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 12 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "top.main_clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.main_clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 15 : i64, repetition_is_unbounded = false} {
                obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$past", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "top.data", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.data, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "top.gate", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.gate, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.clocking_event attributes {is_signed = false, node_id = 20 : i64, semantic_type = !obelisk.void} {
                    obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = true, node_id = 21 : i64} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "top.alternate_clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.alternate_clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "top.qualify", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.qualify, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

// The gate-only and iff-only assertions share the four-argument sampler. Its
// qualification controls suspension and the accepted sample updates history
// unconditionally. Both assertions read the same history identity.
// CHECK-COUNT-2: obelisk_sim.code_unit.decl {{[0-9]+}} in 0 always hierarchy {{.*}} debug "alternate-clock sampler"
// CHECK-NOT: debug "alternate-clock sampler"
// CHECK: obelisk_sim.spawn @[[COMBINED:unit_2[.][$]clocked_sample[.][0-9]+]]({{.*}}, {{.*}}, {{.*}}, {{.*}}, {{.*}})
// CHECK: obelisk_sim.spawn @[[SHARED:unit_0[.][$]clocked_sample[.][0-9]+]]({{.*}}, {{.*}}, {{.*}}, {{.*}})
// CHECK-NOT: obelisk_sim.spawn @{{.*}}[.][$]clocked_sample
// CHECK: obelisk_sim.func private @[[SHARED]](
// CHECK-SAME: %arg0: !obelisk_sim.context
// CHECK-SAME: %arg1: !obelisk_sim.ref<!obelisk_sim.logic<1>>
// CHECK-SAME: %arg2: !obelisk_sim.ref<!obelisk_sim.logic<1>>
// CHECK-SAME: %arg3: !obelisk_sim.ref<!obelisk_sim.logic<1>>
// CHECK: obelisk_sim.suspend.edge_iff posedge %arg2 iff %arg3
// CHECK-SAME: resume_region = 16 : i32
// CHECK: %[[SHARED_SOURCE:.*]] = obelisk_sim.assert.sampled_read %arg0 from %arg1
// CHECK-NEXT: %[[TRUE:.*]] = arith.constant {{.*}}true
// CHECK-NEXT: obelisk_sim.assert.clocked_sample_update %arg0 from %[[SHARED_SOURCE]] gate %[[TRUE]] id [[SHARED_ID:[0-9]+]] depth 2
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.assert.clocked_sample_read {{.*}} id [[SHARED_ID]] depth 2 age 1
// CHECK: obelisk_sim.assert.clocked_sample_read {{.*}} id [[SHARED_ID]] depth 2 age 1
// CHECK-NOT: obelisk_sim.assert.clocked_sample_read

// The simultaneous form captures source, clock, iff, and gate separately.
// Only the iff controls suspension; the gate is sampled from the same
// Preponed snapshot and reaches the update as an independent i1.
// CHECK: obelisk_sim.func private @[[COMBINED]](
// CHECK-SAME: %arg0: !obelisk_sim.context
// CHECK-SAME: %arg1: !obelisk_sim.ref<!obelisk_sim.logic<1>>
// CHECK-SAME: %arg2: !obelisk_sim.ref<!obelisk_sim.logic<1>>
// CHECK-SAME: %arg3: !obelisk_sim.ref<!obelisk_sim.logic<1>>
// CHECK-SAME: %arg4: !obelisk_sim.ref<!obelisk_sim.logic<1>>
// CHECK: obelisk_sim.suspend.edge_iff posedge %arg2 iff %arg3
// CHECK-SAME: resume_region = 16 : i32
// CHECK: %[[SOURCE:.*]] = obelisk_sim.assert.sampled_read %arg0 from %arg1
// CHECK-NEXT: %[[GATE:.*]] = obelisk_sim.assert.sampled_read %arg0 from %arg4
// CHECK-NEXT: %[[GATE_TRUTH:.*]] = obelisk_sim.logic.is_true %[[GATE]]
// CHECK-NEXT: obelisk_sim.assert.clocked_sample_update %arg0 from %[[SOURCE]] gate %[[GATE_TRUTH]] id [[COMBINED_ID:[0-9]+]] depth 2
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.assert.clocked_sample_read {{.*}} id [[COMBINED_ID]] depth 2 age 1
// CHECK-NOT: obelisk_sim.assert.clocked_sample_read
