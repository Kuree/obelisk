// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// An asynchronous accept_on over bounded state.  Both report actions compare
// their captured epoch before executing.
// CHECK-LABEL: obelisk_sim.func private @unit_0.fork.18.0.0(
// CHECK: %[[U0_PASS_EPOCH:[0-9]+]] = obelisk_sim.ref.load %arg2
// CHECK: %[[U0_PASS_VALID:[0-9]+]] = arith.cmpi eq, %[[U0_PASS_EPOCH]], %arg3
// CHECK: cf.cond_br %[[U0_PASS_VALID]],
// CHECK-LABEL: obelisk_sim.func private @unit_0.fork.18.1.1(
// CHECK: %[[U0_FAIL_EPOCH:[0-9]+]] = obelisk_sim.ref.load %arg2
// CHECK: %[[U0_FAIL_VALID:[0-9]+]] = arith.cmpi eq, %[[U0_FAIL_EPOCH]], %arg3
// CHECK: cf.cond_br %[[U0_FAIL_VALID]],

// The asynchronous disable observer clears the complete bounded state and
// advances the epoch before returning to its priority wait.
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_cancel.18(
// CHECK: obelisk_sim.ref.store {{.*}} to %arg4
// CHECK: %[[U0_OLD_EPOCH:[0-9]+]] = obelisk_sim.ref.load %arg5
// CHECK: %[[U0_NEW_EPOCH:[0-9]+]] = arith.addi %[[U0_OLD_EPOCH]],
// CHECK: obelisk_sim.ref.store %[[U0_NEW_EPOCH]] to %arg5

// Each asynchronous abort completion reloads the current disable epoch and
// passes that value to the queued report.  There is one site for each live age.
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_abort.18(
// CHECK-SAME: %arg5: !obelisk_sim.ref<i64> {obelisk_sim.automatic_reference_capture, obelisk_sim.capture_kind = 1 : i32}) attributes
// CHECK: %[[U0_ABORT_EPOCH0:[0-9]+]] = obelisk_sim.ref.load %arg5
// CHECK-NEXT: {{.*}}obelisk_sim.spawn @unit_0.fork.18.0.0(%arg0, %arg4, %arg5, %[[U0_ABORT_EPOCH0]])
// CHECK: %[[U0_ABORT_EPOCH1:[0-9]+]] = obelisk_sim.ref.load %arg5
// CHECK-NEXT: {{.*}}obelisk_sim.spawn @unit_0.fork.18.0.0(%arg0, %arg4, %arg5, %[[U0_ABORT_EPOCH1]])
// CHECK-NOT: obelisk_sim.ref.load %arg5

// On a property clock, the current unsampled disable condition branches before
// the sampled abort condition.  The same state/epoch pair is shared by both
// priority observers and every report.
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.asynchronous_property_abort
// CHECK-SAME: obelisk_sim.property_abort_action = "accept"
// CHECK: obelisk_sim.spawn @unit_0.$concurrent_cancel.18
// CHECK: obelisk_sim.context.event
// CHECK: obelisk_sim.spawn @unit_0.$concurrent_abort.18
// CHECK: %[[U0_RESET:[0-9]+]] = obelisk_sim.ref.load %arg4
// CHECK-NEXT: %[[U0_RESET_TRUE:[0-9]+]] = obelisk_sim.logic.is_true %[[U0_RESET]]
// CHECK-NEXT: cf.cond_br %[[U0_RESET_TRUE]], ^bb3
// CHECK: ^bb4:
// CHECK-NEXT: %[[U0_KILL:[0-9]+]] = obelisk_sim.assert.sampled_read %arg0 from %arg5

// sync_reject_on needs no asynchronous abort actor, but shares the identical
// cancellation state and epoch.  Its sampled forced result queues the fail
// action with an expected epoch only after the disable branch was false.
// CHECK-NOT: @unit_1.$concurrent_abort
// CHECK-LABEL: obelisk_sim.func private @unit_1.$concurrent_cancel.38(
// CHECK: obelisk_sim.ref.store {{.*}} to %arg4
// CHECK: %[[U1_OLD_EPOCH:[0-9]+]] = obelisk_sim.ref.load %arg5
// CHECK: %[[U1_NEW_EPOCH:[0-9]+]] = arith.addi %[[U1_OLD_EPOCH]],
// CHECK: obelisk_sim.ref.store %[[U1_NEW_EPOCH]] to %arg5
// CHECK-NOT: @unit_1.$concurrent_abort
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.property_abort_action = "reject"
// CHECK-SAME: obelisk_sim.synchronous_property_abort
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK: %[[U1_RESET:[0-9]+]] = obelisk_sim.ref.load %arg4
// CHECK-NEXT: %[[U1_RESET_TRUE:[0-9]+]] = obelisk_sim.logic.is_true %[[U1_RESET]]
// CHECK-NEXT: cf.cond_br %[[U1_RESET_TRUE]], ^bb3
// CHECK: ^bb4:
// CHECK-NEXT: %[[U1_KILL:[0-9]+]] = obelisk_sim.assert.sampled_read %arg0 from %arg5
// CHECK: ^bb5:
// CHECK: obelisk_sim.spawn @unit_1.fork.38.1.1(%arg0, %arg6, {{%[0-9]+}}, {{%[0-9]+}})

// The persistent-delay monitor owns warm-up and eligible cells.  Disable clears
// both and advances one epoch.  The shared counted abort dispatcher also clears
// both, but reloads that epoch once for every dynamically counted fail report.
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_cancel.58(
// CHECK: obelisk_sim.ref.store {{.*}} to %arg4
// CHECK: obelisk_sim.ref.store {{.*}} to %arg5
// CHECK: %[[U2_OLD_EPOCH:[0-9]+]] = obelisk_sim.ref.load %arg6
// CHECK: %[[U2_NEW_EPOCH:[0-9]+]] = arith.addi %[[U2_OLD_EPOCH]],
// CHECK: obelisk_sim.ref.store %[[U2_NEW_EPOCH]] to %arg6
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_abort_count.58.reject(
// CHECK: %[[U2_WARM:[0-9]+]] = obelisk_sim.ref.load %arg1
// CHECK: obelisk_sim.ref.store {{.*}} to %arg1
// CHECK: %[[U2_ELIGIBLE:[0-9]+]] = obelisk_sim.ref.load %arg2
// CHECK-NEXT: obelisk_sim.ref.store {{.*}} to %arg2
// CHECK: %[[U2_ABORT_EPOCH:[0-9]+]] = obelisk_sim.ref.load %arg4
// CHECK-NEXT: {{.*}}obelisk_sim.spawn @unit_2.fork.58.1.1(%arg0, %arg3, %arg4, %[[U2_ABORT_EPOCH]])
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_abort.58(
// CHECK: obelisk_sim.call @unit_2.$concurrent_abort_count.58.reject(%arg0, %arg3, %arg4, %arg5, %arg6,
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.asynchronous_property_abort
// CHECK-SAME: obelisk_sim.persistent_delay_aggregate_tokens
// CHECK-SAME: obelisk_sim.property_abort_action = "reject"
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK: %[[U2_RESET:[0-9]+]] = obelisk_sim.ref.load %arg4
// CHECK-NEXT: %[[U2_RESET_TRUE:[0-9]+]] = obelisk_sim.logic.is_true %[[U2_RESET]]
// CHECK-NEXT: cf.cond_br %[[U2_RESET_TRUE]], ^bb3
// CHECK: ^bb4:
// CHECK-NEXT: %[[U2_KILL:[0-9]+]] = obelisk_sim.assert.sampled_read %arg0 from %arg5
// CHECK: ^bb5:
// CHECK: obelisk_sim.call @unit_2.$concurrent_abort_count.58.reject({{.*}}%c1_i64

// The fourth unit covers sync_accept_on and the prior chunk's outer temporal
// negation.  There is still no asynchronous abort actor; accept is forced true
// inside the abort and then inverted, so its clocked abort route selects fail.
// CHECK-NOT: @unit_3.$concurrent_abort
// CHECK-LABEL: obelisk_sim.func private @unit_3.$concurrent_cancel.78(
// CHECK: obelisk_sim.ref.store {{.*}} to %arg4
// CHECK: %[[U3_OLD_EPOCH:[0-9]+]] = obelisk_sim.ref.load %arg5
// CHECK: %[[U3_NEW_EPOCH:[0-9]+]] = arith.addi %[[U3_OLD_EPOCH]],
// CHECK: obelisk_sim.ref.store %[[U3_NEW_EPOCH]] to %arg5
// CHECK-NOT: @unit_3.$concurrent_abort
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: obelisk_sim.property_abort_action = "accept"
// CHECK-SAME: obelisk_sim.synchronous_property_abort
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK-SAME: obelisk_sim.temporal_property_negation_outside_abort
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK: %[[U3_RESET:[0-9]+]] = obelisk_sim.ref.load %arg4
// CHECK-NEXT: %[[U3_RESET_TRUE:[0-9]+]] = obelisk_sim.logic.is_true %[[U3_RESET]]
// CHECK-NEXT: cf.cond_br %[[U3_RESET_TRUE]], ^bb3
// CHECK: ^bb4:
// CHECK-NEXT: %[[U3_KILL:[0-9]+]] = obelisk_sim.assert.sampled_read %arg0 from %arg5
// CHECK: ^bb7:
// CHECK: obelisk_sim.spawn @unit_3.fork.78.1.1(%arg0, %arg6, {{%[0-9]+}}, {{%[0-9]+}})
// CHECK-NOT: @unit_3.$concurrent_abort
// A forced asynchronous accept with no pass action still clears the monitor;
// it needs no expected-epoch operand because it queues no callback.
// CHECK-LABEL: obelisk_sim.func private @unit_4.$concurrent_abort.99(
// CHECK: ^bb2:
// CHECK-NEXT: {{.*}}arith.constant {{.*}} 0 : i64
// CHECK-NEXT: obelisk_sim.ref.store {{.*}} to %arg3
// CHECK-NEXT: cf.br ^bb1
// CHECK-NOT: obelisk_sim.spawn
// CHECK-LABEL: obelisk_sim.func private @unit_4(
// CHECK-SAME: obelisk_sim.asynchronous_property_abort
// CHECK-SAME: obelisk_sim.property_abort_action = "accept"
// A callback-free restrict property is the exact silent boundary: its async
// abort actor captures only context, sampled condition/event, and monitor state.
// The disable epoch exists in the parent but is deliberately absent here.
// CHECK-NOT: @unit_5.fork
// CHECK-LABEL: obelisk_sim.func private @unit_5.$concurrent_abort.114(%arg0: !obelisk_sim.context {{.*}}, %arg1: !obelisk_sim.ref<!obelisk_sim.logic<1>> {{.*}}, %arg2: !obelisk_sim.event {{.*}}, %arg3: !obelisk_sim.ref<i64> {{.*}}) attributes
// CHECK: ^bb2:
// CHECK-NEXT: {{.*}}arith.constant {{.*}} 0 : i64
// CHECK-NEXT: obelisk_sim.ref.store {{.*}} to %arg3
// CHECK-NEXT: cf.br ^bb1
// CHECK-NOT: obelisk_sim.spawn
// CHECK-LABEL: obelisk_sim.func private @unit_5(
// CHECK-SAME: obelisk_sim.asynchronous_property_abort
// CHECK-SAME: obelisk_sim.property_abort_action = "accept"

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
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.reset", name = "reset", node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s11.reset"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.reset", lifetime = 1 : i32, name = "reset", node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s12.reset"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.kill", name = "kill", node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s13.kill"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.kill", lifetime = 1 : i32, name = "kill", node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s14.kill"} {
        }
        obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "top.hit", name = "hit", node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s15.hit"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.hit", lifetime = 1 : i32, name = "hit", node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s16.hit"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 17 : i64, procedure_kind = 2 : i32, sym_name = "s17", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 18 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 19 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 20 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.disable_iff attributes {node_id = 22 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "top.reset", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.abort attributes {action = 0 : i32, is_synchronous = false, node_id = 24 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "top.kill", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.kill, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 26 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 27 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 28 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 29 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 30 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 31 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 32 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 33 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s16.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 34 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 35 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 36 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s16.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 37 : i64, procedure_kind = 2 : i32, sym_name = "s18", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 38 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 39 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 40 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 41 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.disable_iff attributes {node_id = 42 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 43 : i64, referenced_path = "top.reset", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.abort attributes {action = 1 : i32, is_synchronous = true, node_id = 44 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 45 : i64, referenced_path = "top.kill", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.kill, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 46 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 47 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 48 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 49 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 50 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 51 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 52 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 53 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s16.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 54 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 55 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 56 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s16.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 57 : i64, procedure_kind = 2 : i32, sym_name = "s19", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 58 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 59 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 60 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 61 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.disable_iff attributes {node_id = 62 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 63 : i64, referenced_path = "top.reset", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.abort attributes {action = 1 : i32, is_synchronous = false, node_id = 64 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 65 : i64, referenced_path = "top.kill", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.kill, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = true, min = 2 : i64}], node_id = 66 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 67 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 68 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 69 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 70 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 71 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 72 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 73 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s16.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 74 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 75 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 76 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s16.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 77 : i64, procedure_kind = 2 : i32, sym_name = "s20", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 78 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 79 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 80 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 81 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.disable_iff attributes {node_id = 82 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 83 : i64, referenced_path = "top.reset", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 84 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                  obelisk.sv.assertion.abort attributes {action = 0 : i32, is_synchronous = true, node_id = 85 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 86 : i64, referenced_path = "top.kill", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.kill, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 87 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 88 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 89 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 90 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 91 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 92 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 93 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 94 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s16.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 95 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 96 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 97 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s16.hit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 98 : i64, procedure_kind = 2 : i32, sym_name = "s21", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 99 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 100 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 101 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 102 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.disable_iff attributes {node_id = 103 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 104 : i64, referenced_path = "top.reset", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.abort attributes {action = 0 : i32, is_synchronous = false, node_id = 105 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 106 : i64, referenced_path = "top.kill", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.kill, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 107 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 108 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 109 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 110 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 111 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 112 : i64} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 113 : i64, procedure_kind = 2 : i32, sym_name = "s22", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 4 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 114 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 115 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 116 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 117 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.disable_iff attributes {node_id = 118 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 119 : i64, referenced_path = "top.reset", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.abort attributes {action = 0 : i32, is_synchronous = false, node_id = 120 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 121 : i64, referenced_path = "top.kill", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.kill, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 122 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 123 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 124 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 125 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 126 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 127 : i64} {
            }
          }
        }
      }
    }
  }
}
