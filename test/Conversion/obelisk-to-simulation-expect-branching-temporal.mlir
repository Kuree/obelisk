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
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.a"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 6 : i64, procedure_kind = 0 : i32, sym_name = "s6", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 5 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 7 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 8 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 9 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 11 : i64, operator_kind = 1 : i32} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 12 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 13 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 15 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 17 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 18 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 20 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
          }
        }

        // The 13x5 delay product creates 65 exact alternatives, crossing the
        // compact state's 64-bit word boundary while staying under the
        // bounded 63-sample horizon and 256-alternative admission caps.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 30 : i64, procedure_kind = 0 : i32, sym_name = "s30", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 5 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 31 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 32 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 33 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 13 : i64, min = 1 : i64}, {is_unbounded = false, max = 5 : i64, min = 1 : i64}], node_id = 35 : i64} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 36 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 37 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 38 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 39 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 40 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 41 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

// The first clock activates both traces. At age one the shorter trace can
// succeed while the longer trace remains live through its gap; at age two the
// longer trace decides the result. One compact word carries both alternatives.
// CHECK: obelisk_sim.func private @[[MONITOR:[^(]+]](
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: obelisk_sim.expect_bounded_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.expect_bounded_branching
// CHECK-SAME: obelisk_sim.expect_bounded_horizon = 3 : i64
// CHECK-SAME: obelisk_sim.expect_bounded_state_words = 1 : i64
// CHECK: %[[NOT_DONE:.*]] = arith.constant false
// CHECK: %[[DONE_STATE:.*]] = obelisk_sim.ref.alloc %[[NOT_DONE]]
// CHECK: obelisk_sim.spawn @[[MONITOR]].$expect_eos.7
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK: %[[SUCCESS:.*]] = arith.constant {{.*}}true
// CHECK: obelisk_sim.ref.store %[[SUCCESS]]
// CHECK: obelisk_sim.ref.store {{.*}}true
// CHECK-NOT: obelisk_sim.ref.store
// CHECK: obelisk_sim.event.trigger
// CHECK: %[[FAILURE:.*]] = arith.constant {{.*}}false
// CHECK: obelisk_sim.ref.store %[[FAILURE]]
// CHECK: obelisk_sim.ref.store {{.*}}true
// CHECK-NOT: obelisk_sim.ref.store
// CHECK: obelisk_sim.event.trigger
// CHECK-NOT: obelisk_sim.event.trigger
// CHECK: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK: ^bb{{.*}}(%{{.*}}: i64)
// CHECK: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK: ^bb{{.*}}(%{{.*}}: i64)
// CHECK: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.suspend.edge

// A final-phase coordinator applies expect's implicit strong(sequence)
// semantics. It does nothing before the first subsequent clock or after an
// ordinary completion. A started, pending evaluation stores failure, is marked
// done, and wakes the same procedural caller for its Reactive fail action.
// CHECK: obelisk_sim.func private @[[MONITOR]].$expect_eos.7(
// CHECK-SAME: entry_kind = 2 : i32
// CHECK-SAME: obelisk_sim.expect_eos_coordinator
// CHECK-SAME: obelisk_sim.expect_operand_strength = "strong"
// CHECK: %[[EOS_WAS_DONE:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: cf.cond_br %[[EOS_WAS_DONE]]
// CHECK: %[[EOS_WAS_STARTED:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: cf.cond_br %[[EOS_WAS_STARTED]]
// CHECK: %[[EOS_FAILURE:.*]] = arith.constant {{.*}}false
// CHECK: obelisk_sim.ref.store %[[EOS_FAILURE]]
// CHECK: %[[EOS_DONE:.*]] = arith.constant {{.*}}true
// CHECK: obelisk_sim.ref.store %[[EOS_DONE]]
// CHECK: obelisk_sim.event.trigger
// CHECK-NOT: obelisk_sim.event.trigger

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK: %[[DONE:.*]] = obelisk_sim.event.create
// CHECK: obelisk_sim.spawn @[[MONITOR]]
// CHECK: obelisk_sim.suspend.event %[[DONE]]
// CHECK-SAME: resume_region = 10 : i32

// The ranged-delay product materializes 65 distinct alternatives. Alternative
// 63 uses the high bit of word zero, while alternative 64 uses bit zero of word
// one. Both words are carried as SSA values across subsequent suspensions.
// CHECK: obelisk_sim.func private @[[WIDE_MONITOR:unit_1.fork.31.0.16]](
// CHECK-SAME: obelisk_sim.expect_bounded_alternatives = 65 : i64
// CHECK-SAME: obelisk_sim.expect_bounded_branching
// CHECK-SAME: obelisk_sim.expect_bounded_horizon = 19 : i64
// CHECK-SAME: obelisk_sim.expect_bounded_state_words = 2 : i64
// CHECK: %[[HIGH_RETAIN_MASK:.*]] = arith.constant {obelisk_sim.rematerialized} -9223372036854775808 : i64
// CHECK-NEXT: %{{.*}} = arith.select {{.*}}, %[[HIGH_RETAIN_MASK]], {{.*}} : i64
// CHECK: obelisk_sim.suspend.edge posedge {{.*}} to ^[[WIDE_RESUME:bb[0-9]+]]({{.*}}, %[[SECOND_NEXT:.*]], %[[FIRST_NEXT:.*]] : !obelisk_sim.ref<i1>, i64, i64)
// CHECK: ^[[WIDE_RESUME]]({{.*}}: !obelisk_sim.ref<i1>, %[[SECOND_WORD:.*]]: i64, %[[FIRST_WORD:.*]]: i64)
// CHECK: %[[HIGH_TEST_MASK:.*]] = arith.constant {obelisk_sim.rematerialized} -9223372036854775808 : i64
// CHECK-NEXT: %{{.*}} = arith.andi %[[FIRST_WORD]], %[[HIGH_TEST_MASK]] : i64
// CHECK: %[[SECOND_TEST_MASK:.*]] = arith.constant {obelisk_sim.rematerialized} 1 : i64
// CHECK-NEXT: %{{.*}} = arith.andi %[[SECOND_WORD]], %[[SECOND_TEST_MASK]] : i64
