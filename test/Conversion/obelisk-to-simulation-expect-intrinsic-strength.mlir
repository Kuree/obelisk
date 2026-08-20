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

        // Weak nexttime succeeds if its operand remains pending at EOS.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 0 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 5 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 11 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 12 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 15 : i64, operator_kind = 1 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 16 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
        }

        // Strong nexttime[2] fails if its operand remains pending at EOS.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 20 : i64, procedure_kind = 0 : i32, sym_name = "s20", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 5 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 21 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 22 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 23 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 24 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = true, node_id = 25 : i64, operator_kind = 2 : i32, range_is_unbounded = false, range_max = 2 : i64, range_min = 2 : i64} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 26 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 27 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
        }

        // Temporal not preserves the weak intrinsic completion of nexttime,
        // then inverts its pending success to an outer strong failure.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 30 : i64, procedure_kind = 0 : i32, sym_name = "s30", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 5 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 31 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 32 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 33 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 35 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 36 : i64, operator_kind = 1 : i32, range_is_unbounded = false} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 37 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 38 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

// Both live evaluators start only on the subsequent clock, mark that start for
// EOS, and retain their ordinary predicate result on the selected future age.
// CHECK: obelisk_sim.func private @unit_0.fork.11.0.16(
// CHECK-SAME: obelisk_sim.expect_monitor_actor
// CHECK: obelisk_sim.spawn @unit_0.fork.11.0.16.$expect_eos.11
// CHECK-COUNT-2: obelisk_sim.suspend.edge posedge
// CHECK: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.func private @unit_0.fork.11.0.16.$expect_eos.11(
// CHECK-SAME: obelisk_sim.expect_eos_coordinator
// CHECK-SAME: obelisk_sim.expect_operand_strength = "weak"
// CHECK: %[[WEAK_DONE:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: cf.cond_br %[[WEAK_DONE]]
// CHECK: %[[WEAK_STARTED:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: cf.cond_br %[[WEAK_STARTED]]
// CHECK: %[[WEAK_RESULT:.*]] = arith.constant {{.*}}true
// CHECK-NEXT: obelisk_sim.ref.store %[[WEAK_RESULT]]
// CHECK: obelisk_sim.event.trigger

// CHECK: obelisk_sim.func private @unit_1.fork.21.0.16(
// CHECK-SAME: obelisk_sim.expect_monitor_actor
// CHECK: obelisk_sim.spawn @unit_1.fork.21.0.16.$expect_eos.21
// CHECK-COUNT-3: obelisk_sim.suspend.edge posedge
// CHECK: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.func private @unit_1.fork.21.0.16.$expect_eos.21(
// CHECK-SAME: obelisk_sim.expect_eos_coordinator
// CHECK-SAME: obelisk_sim.expect_operand_strength = "strong"
// CHECK: %[[STRONG_DONE:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: cf.cond_br %[[STRONG_DONE]]
// CHECK: %[[STRONG_STARTED:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: cf.cond_br %[[STRONG_STARTED]]
// CHECK: %[[STRONG_RESULT:.*]] = arith.constant {{.*}}false
// CHECK-NEXT: obelisk_sim.ref.store %[[STRONG_RESULT]]
// CHECK: obelisk_sim.event.trigger

// Intrinsic weak completion composes with temporal not exactly like explicit
// weak qualification: live success/failure invert, and pending EOS fails.
// CHECK: obelisk_sim.func private @unit_2.fork.31.0.16(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.expect_monitor_actor
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.strong_weak_monitor
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK: ^bb1(
// CHECK: %[[NOT_NEXT_LIVE_FAIL:.*]] = arith.constant {{.*}}false
// CHECK-NEXT: obelisk_sim.ref.store %[[NOT_NEXT_LIVE_FAIL]] to %arg4
// CHECK: obelisk_sim.event.trigger
// CHECK: %[[NOT_NEXT_LIVE_PASS:.*]] = arith.constant {{.*}}true
// CHECK-NEXT: obelisk_sim.ref.store %[[NOT_NEXT_LIVE_PASS]] to %arg4
// CHECK: obelisk_sim.event.trigger
// CHECK: obelisk_sim.func private @unit_2.fork.31.0.16.$expect_eos.31(
// CHECK-SAME: obelisk_sim.expect_eos_coordinator
// CHECK-SAME: obelisk_sim.expect_operand_strength = "weak"
// CHECK: %[[NOT_NEXT_DONE:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: cf.cond_br %[[NOT_NEXT_DONE]]
// CHECK: %[[NOT_NEXT_STARTED:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: cf.cond_br %[[NOT_NEXT_STARTED]]
// CHECK: %[[NOT_NEXT_EOS_FAIL:.*]] = arith.constant {{.*}}false
// CHECK-NEXT: obelisk_sim.ref.store %[[NOT_NEXT_EOS_FAIL]]
// CHECK: obelisk_sim.event.trigger
