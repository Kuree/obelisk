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
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.rst", lifetime = 1 : i32, name = "rst", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.rst"} {
        }

        // a |-> ((b or c) or (b and c)); the third trace is subsumed.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 2 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 11 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 12 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 15 : i64, operator_kind = 11 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 16 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 18 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.binary attributes {node_id = 19 : i64, operator_kind = 1 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 20 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 22 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.assertion.binary attributes {node_id = 24 : i64, operator_kind = 0 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 25 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 26 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 27 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 28 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
          }
        }

        // a |=> (b ##[1:2] c)
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 30 : i64, procedure_kind = 2 : i32, sym_name = "s30", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 31 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 32 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 33 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.disable_iff attributes {node_id = 35 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 36 : i64, referenced_path = "top.rst", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.rst, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.binary attributes {node_id = 37 : i64, operator_kind = 12 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 38 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 39 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 40 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 41 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 42 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 43 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 44 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
          }
        }

        // disable iff (rst) ((a or b) |=> c)
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 50 : i64, procedure_kind = 2 : i32, sym_name = "s50", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 51 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 52 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 53 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 54 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.disable_iff attributes {node_id = 55 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 56 : i64, referenced_path = "top.rst", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.rst, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.binary attributes {node_id = 57 : i64, operator_kind = 12 : i32} {
                  obelisk.sv.assertion.binary attributes {node_id = 58 : i64, operator_kind = 1 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 59 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 60 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 61 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 62 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 63 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 64 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
          }
        }

        // disable iff (rst) (b ##[1:2] c)
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 70 : i64, procedure_kind = 2 : i32, sym_name = "s70", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 71 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 72 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 73 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 74 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.disable_iff attributes {node_id = 75 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 76 : i64, referenced_path = "top.rst", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.rst, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 77 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 78 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 79 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 80 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 81 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
          }
        }

        // disable iff (rst) (b or c): no live state word is required.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 90 : i64, procedure_kind = 2 : i32, sym_name = "s90", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 91 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 92 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 93 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 94 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.disable_iff attributes {node_id = 95 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 96 : i64, referenced_path = "top.rst", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.rst, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.binary attributes {node_id = 97 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 98 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 99 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 100 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 101 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

// The overlapped consequent samples both exact alternatives on the antecedent
// clock. The alternatives share one property result, not one runtime thread.
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: obelisk_sim.branching_consequent_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_consequent_monitor
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_after = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_before = 3 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_solver = "{{(heuristic|z3)}}"
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK: [[A_SAMPLE:%.*]] = obelisk_sim.assert.sampled_read %arg0 from %arg2
// CHECK: [[A_MATCH:%.*]] = obelisk_sim.logic.is_true [[A_SAMPLE]]
// CHECK: [[B_SAMPLE:%.*]] = obelisk_sim.assert.sampled_read %arg0 from %arg3
// CHECK: [[B_MATCH:%.*]] = obelisk_sim.logic.is_true [[B_SAMPLE]]
// CHECK: [[AB:%.*]] = arith.andi [[A_MATCH]], [[B_MATCH]] {{.*}}obelisk_sim.branching_consequent_trigger
// CHECK: [[C_SAMPLE:%.*]] = obelisk_sim.assert.sampled_read %arg0 from %arg4
// CHECK: [[C_MATCH:%.*]] = obelisk_sim.logic.is_true [[C_SAMPLE]]
// CHECK: [[AC:%.*]] = arith.andi [[A_MATCH]], [[C_MATCH]] {{.*}}obelisk_sim.branching_consequent_trigger
// CHECK: arith.ori [[AB]], [[AC]]

// Nonoverlap first stores a bit-0 obligation, then independently advances the
// two ranged exact traces. The asynchronous disable actor clears both exact
// trace states, and the clock path checks the unsampled level before sampling.
// CHECK-LABEL: obelisk_sim.func private @unit_1.$concurrent_cancel.31(
// CHECK-SAME: obelisk_sim.concurrent_cancel
// CHECK-SAME: obelisk_sim.detached_controls
// CHECK-SAME: obelisk_sim.priority_signal_resume
// CHECK: [[CANCEL_ZERO0:%.*]] = arith.constant {{.*}} 0 : i64
// CHECK-NEXT: obelisk_sim.ref.store [[CANCEL_ZERO0]] to %arg4
// CHECK: [[CANCEL_ZERO1:%.*]] = arith.constant {{.*}} 0 : i64
// CHECK-NEXT: obelisk_sim.ref.store [[CANCEL_ZERO1]] to %arg5
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: obelisk_sim.branching_consequent_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_consequent_monitor
// CHECK-SAME: obelisk_sim.branching_consequent_nonoverlapped
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK: obelisk_sim.ref.load %arg5
// CHECK: cf.cond_br
// CHECK: ^bb{{[0-9]+}}([[STATE0_REF:%[0-9]+]]: !obelisk_sim.ref<i64>, [[STATE1_REF:%[0-9]+]]: !obelisk_sim.ref<i64>):
// CHECK: [[A_SAMPLE_1:%.*]] = obelisk_sim.assert.sampled_read %arg0 from %arg2
// CHECK: [[A_MATCH_1:%.*]] = obelisk_sim.logic.is_true [[A_SAMPLE_1]]
// CHECK: [[STATE0:%.*]] = obelisk_sim.ref.load [[STATE0_REF]]
// CHECK: [[STATE0_AGE0:%.*]] = arith.andi [[STATE0]], {{%.*}} : i64
// CHECK: [[ACTIVE0:%.*]] = arith.cmpi ne, [[STATE0_AGE0]], {{%.*}} : i64
// CHECK: [[B_SAMPLE_1:%.*]] = obelisk_sim.assert.sampled_read %arg0 from %arg3
// CHECK: [[B_MATCH_1:%.*]] = obelisk_sim.logic.is_true [[B_SAMPLE_1]]
// CHECK: [[ADVANCE0:%.*]] = arith.andi [[ACTIVE0]], [[B_MATCH_1]] : i1
// CHECK: [[STATE1:%.*]] = obelisk_sim.ref.load [[STATE1_REF]]
// CHECK: [[STATE1_AGE0:%.*]] = arith.andi [[STATE1]], {{%.*}} : i64
// CHECK: [[ACTIVE1:%.*]] = arith.cmpi ne, [[STATE1_AGE0]], {{%.*}} : i64
// CHECK: arith.ori [[ACTIVE0]], [[ACTIVE1]] : i1
// CHECK: [[ADVANCE1:%.*]] = arith.andi [[ACTIVE1]], [[B_MATCH_1]] : i1
// CHECK: [[LAUNCH:%.*]] = arith.extui [[A_MATCH_1]] : i1 to i64
// CHECK: [[ADVANCE0_STATE:%.*]] = arith.select [[ADVANCE0]], {{%.*}}, {{%.*}} : i64
// CHECK: [[NEXT0:%.*]] = arith.ori [[LAUNCH]], [[ADVANCE0_STATE]] : i64
// CHECK: [[ADVANCE1_STATE:%.*]] = arith.select [[ADVANCE1]], {{%.*}}, {{%.*}} : i64
// CHECK: [[NEXT1_BASE:%.*]] = arith.ori [[LAUNCH]], [[ADVANCE1_STATE]] : i64
// CHECK: [[NEXT1:%.*]] = arith.ori [[NEXT1_BASE]], {{%.*}} : i64
// CHECK: obelisk_sim.ref.store [[NEXT0]] to [[NEXT0_REF:%[0-9]+]]
// CHECK: obelisk_sim.ref.store [[NEXT1]] to [[NEXT1_REF:%[0-9]+]]
// CHECK: cf.br {{.*}}({{%[0-9]+}}, [[NEXT0_REF]], [[NEXT1_REF]] : {{.*}}) {{.*}}obelisk_sim.branching_consequent_backedge

// A same-endpoint branching antecedent owns two independent nonoverlapped
// consequent channels. Its disable actor clears both channels plus the epoch.
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_cancel.51(
// CHECK-SAME: obelisk_sim.concurrent_cancel
// CHECK: [[ANT_ZERO0:%.*]] = arith.constant {{.*}} 0 : i64
// CHECK-NEXT: obelisk_sim.ref.store [[ANT_ZERO0]] to %arg4
// CHECK: [[ANT_ZERO1:%.*]] = arith.constant {{.*}} 0 : i64
// CHECK-NEXT: obelisk_sim.ref.store [[ANT_ZERO1]] to %arg5
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.branching_antecedent_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_match_channels = 2 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_monitor
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK: obelisk_sim.ref.load %arg5
// CHECK: cf.cond_br
// CHECK: cf.br {{.*}}obelisk_sim.branching_antecedent_backedge

// A ranged plain sequence uses the same cancellation contract and clears both
// alternative state words before the monitor can start a disabled attempt.
// CHECK-LABEL: obelisk_sim.func private @unit_3.$concurrent_cancel.71(
// CHECK-SAME: obelisk_sim.concurrent_cancel
// CHECK: [[SEQ_ZERO0:%.*]] = arith.constant {{.*}} 0 : i64
// CHECK-NEXT: obelisk_sim.ref.store [[SEQ_ZERO0]] to %arg4
// CHECK: [[SEQ_ZERO1:%.*]] = arith.constant {{.*}} 0 : i64
// CHECK-NEXT: obelisk_sim.ref.store [[SEQ_ZERO1]] to %arg5
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_sequence_monitor
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK: obelisk_sim.ref.load %arg4
// CHECK: cf.cond_br

// The all-one-age form has no alternative state captures. The cancellation
// actor still advances its epoch, and the monitor level-gates both predicates.
// CHECK-LABEL: obelisk_sim.func private @unit_4.$concurrent_cancel.91(
// CHECK-SAME: %arg4: !obelisk_sim.ref<i64>
// CHECK-SAME: obelisk_sim.concurrent_cancel
// CHECK-NOT: obelisk_sim.ref.store {{.*}} to %arg{{[0-3]}}
// CHECK: obelisk_sim.ref.store {{.*}} to %arg4
// CHECK-LABEL: obelisk_sim.func private @unit_4(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_sequence_monitor
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK: obelisk_sim.ref.load %arg4
// CHECK: cf.cond_br
// CHECK-NOT: obelisk.sv.assertion
