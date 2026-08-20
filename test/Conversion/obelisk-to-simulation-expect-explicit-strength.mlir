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

        // Explicit weak completion succeeds when first_match(a ##2 b) is
        // still pending. IEEE 16.12.2 makes this deterministic first_match
        // truth-equivalent to weak(a ##2 b).
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 0 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 5 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 11 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 12 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.strong_weak attributes {node_id = 15 : i64, strength = 1 : i32} {
                obelisk.sv.assertion.first_match attributes {match_item_count = 0 : i64, node_id = 16 : i64} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 17 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 18 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 20 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
          }
        }

        // Explicit strong completion also applies to bounded alternatives.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 30 : i64, procedure_kind = 0 : i32, sym_name = "s30", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 5 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 31 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 32 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 33 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.strong_weak attributes {node_id = 35 : i64, strength = 0 : i32} {
                obelisk.sv.assertion.binary attributes {node_id = 36 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 37 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 38 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 39 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 40 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 41 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 42 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 43 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 44 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 45 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 46 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
          }
        }

        // Temporal not inverts both ordinary completion and the strong
        // operand's pending EOS failure. The outer property is weak.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 50 : i64, procedure_kind = 0 : i32, sym_name = "s50", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 5 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 51 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 52 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 53 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 54 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 55 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.strong_weak attributes {node_id = 56 : i64, strength = 0 : i32} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 57 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 58 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 59 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 60 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 61 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
          }
        }

        // The same inversion applies once to a compact branching operand.
        // Weak pending success becomes an outer strong failure.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 70 : i64, procedure_kind = 0 : i32, sym_name = "s70", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 5 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 71 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 72 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 73 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 74 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 75 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.strong_weak attributes {node_id = 76 : i64, strength = 1 : i32} {
                  obelisk.sv.assertion.binary attributes {node_id = 77 : i64, operator_kind = 1 : i32} {
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 78 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 79 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 80 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 81 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 82 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 83 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 84 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 85 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 86 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 87 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }

        // Expect applies strong sequence semantics by default. Temporal not
        // therefore makes this unqualified bounded operand weak.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 90 : i64, procedure_kind = 0 : i32, sym_name = "s90", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 5 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 91 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 92 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 93 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 94 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 95 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 96 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 97 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 98 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 99 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 100 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

// Weak qualification and deterministic first_match are recorded on the
// monitor. Its started/pending EOS path stores success before waking the same
// procedural expect caller.
// CHECK: obelisk_sim.func private @unit_0.fork.11.0.16(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.expect_monitor_actor
// CHECK-SAME: obelisk_sim.first_match_monitor
// CHECK-SAME: obelisk_sim.strong_weak_monitor
// CHECK-COUNT-3: obelisk_sim.suspend.edge posedge
// CHECK: obelisk_sim.func private @unit_0.fork.11.0.16.$expect_eos.11(
// CHECK-SAME: obelisk_sim.expect_eos_coordinator
// CHECK-SAME: obelisk_sim.expect_operand_strength = "weak"
// CHECK: %[[WEAK_DONE:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: cf.cond_br %[[WEAK_DONE]]
// CHECK: %[[WEAK_STARTED:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: cf.cond_br %[[WEAK_STARTED]]
// CHECK: %[[WEAK_PASS:.*]] = arith.constant {{.*}}true
// CHECK-NEXT: obelisk_sim.ref.store %[[WEAK_PASS]]
// CHECK: obelisk_sim.event.trigger

// Strong qualification retains compact branching state and closes a pending
// evaluation as failure at EOS.
// CHECK: obelisk_sim.func private @unit_1.fork.31.0.16(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.expect_bounded_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.expect_bounded_branching
// CHECK-SAME: obelisk_sim.expect_bounded_horizon = 3 : i64
// CHECK-SAME: obelisk_sim.expect_bounded_state_words = 1 : i64
// CHECK-SAME: obelisk_sim.expect_monitor_actor
// CHECK-SAME: obelisk_sim.strong_weak_monitor
// CHECK: obelisk_sim.func private @unit_1.fork.31.0.16.$expect_eos.31(
// CHECK-SAME: obelisk_sim.expect_eos_coordinator
// CHECK-SAME: obelisk_sim.expect_operand_strength = "strong"
// CHECK: %[[STRONG_DONE:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: cf.cond_br %[[STRONG_DONE]]
// CHECK: %[[STRONG_STARTED:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: cf.cond_br %[[STRONG_STARTED]]
// CHECK: %[[STRONG_FAIL:.*]] = arith.constant {{.*}}false
// CHECK-NEXT: obelisk_sim.ref.store %[[STRONG_FAIL]]
// CHECK: obelisk_sim.event.trigger

// A strong deterministic operand becomes a weak negated expect. The live
// success/failure stores and pending EOS result are all inverted once.
// CHECK: obelisk_sim.func private @unit_2.fork.51.0.16(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.expect_monitor_actor
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.strong_weak_monitor
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK: ^bb1(
// CHECK: %[[NOT_STRONG_LIVE_FAIL:.*]] = arith.constant {{.*}}false
// CHECK-NEXT: obelisk_sim.ref.store %[[NOT_STRONG_LIVE_FAIL]] to %arg5
// CHECK: obelisk_sim.event.trigger
// CHECK: %[[NOT_STRONG_LIVE_PASS:.*]] = arith.constant {{.*}}true
// CHECK-NEXT: obelisk_sim.ref.store %[[NOT_STRONG_LIVE_PASS]] to %arg5
// CHECK: obelisk_sim.event.trigger
// CHECK: obelisk_sim.func private @unit_2.fork.51.0.16.$expect_eos.51(
// CHECK-SAME: obelisk_sim.expect_eos_coordinator
// CHECK-SAME: obelisk_sim.expect_operand_strength = "strong"
// CHECK: %[[NOT_STRONG_DONE:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: cf.cond_br %[[NOT_STRONG_DONE]]
// CHECK: %[[NOT_STRONG_STARTED:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: cf.cond_br %[[NOT_STRONG_STARTED]]
// CHECK: %[[NOT_STRONG_EOS_PASS:.*]] = arith.constant {{.*}}true
// CHECK-NEXT: obelisk_sim.ref.store %[[NOT_STRONG_EOS_PASS]]
// CHECK: obelisk_sim.event.trigger

// A weak branching operand keeps its compact two-alternative state. Negation
// makes the outer pending result strong failure.
// CHECK: obelisk_sim.func private @unit_3.fork.71.0.16(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.expect_bounded_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.expect_bounded_branching
// CHECK-SAME: obelisk_sim.expect_bounded_horizon = 3 : i64
// CHECK-SAME: obelisk_sim.expect_bounded_state_words = 1 : i64
// CHECK-SAME: obelisk_sim.expect_monitor_actor
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.strong_weak_monitor
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK: ^bb1(
// CHECK: %[[NOT_WEAK_LIVE_FAIL:.*]] = arith.constant {{.*}}false
// CHECK-NEXT: obelisk_sim.ref.store %[[NOT_WEAK_LIVE_FAIL]] to %arg5
// CHECK: obelisk_sim.event.trigger
// CHECK: %[[NOT_WEAK_LIVE_PASS:.*]] = arith.constant {{.*}}true
// CHECK-NEXT: obelisk_sim.ref.store %[[NOT_WEAK_LIVE_PASS]] to %arg5
// CHECK: obelisk_sim.event.trigger
// CHECK: obelisk_sim.func private @unit_3.fork.71.0.16.$expect_eos.71(
// CHECK-SAME: obelisk_sim.expect_eos_coordinator
// CHECK-SAME: obelisk_sim.expect_operand_strength = "weak"
// CHECK: %[[NOT_WEAK_DONE:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: cf.cond_br %[[NOT_WEAK_DONE]]
// CHECK: %[[NOT_WEAK_STARTED:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: cf.cond_br %[[NOT_WEAK_STARTED]]
// CHECK: %[[NOT_WEAK_EOS_FAIL:.*]] = arith.constant {{.*}}false
// CHECK-NEXT: obelisk_sim.ref.store %[[NOT_WEAK_EOS_FAIL]]
// CHECK: obelisk_sim.event.trigger

// Unqualified expect uses the default strong operand route rather than the
// explicit-strength optional. Temporal not still yields weak/true at EOS.
// CHECK: obelisk_sim.func private @unit_4.fork.91.0.16(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.expect_monitor_actor
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.strong_weak_monitor
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK: obelisk_sim.func private @unit_4.fork.91.0.16.$expect_eos.91(
// CHECK-SAME: obelisk_sim.expect_eos_coordinator
// CHECK-SAME: obelisk_sim.expect_operand_strength = "strong"
// CHECK: %[[NOT_DEFAULT_DONE:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: cf.cond_br %[[NOT_DEFAULT_DONE]]
// CHECK: %[[NOT_DEFAULT_STARTED:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: cf.cond_br %[[NOT_DEFAULT_STARTED]]
// CHECK: %[[NOT_DEFAULT_EOS_PASS:.*]] = arith.constant {{.*}}true
// CHECK-NEXT: obelisk_sim.ref.store %[[NOT_DEFAULT_EOS_PASS]]
// CHECK: obelisk_sim.event.trigger
