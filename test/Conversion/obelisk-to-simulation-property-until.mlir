// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' -o %t.threaded
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' --mlir-disable-threading -o %t.serial
// RUN: diff %t.threaded %t.serial

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
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.hit", lifetime = 1 : i32, name = "hit", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.hit"} {
        }

        // Plain until reports every accumulated attempt through counted
        // Reactive callbacks when b succeeds or (!b && !a) fails.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 2 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 11 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 12 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 15 : i64, operator_kind = 6 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 16 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 18 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 20 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 21 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.hit, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 24 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 25 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 26 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.hit, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 27 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
          }
        }

        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 30 : i64, procedure_kind = 2 : i32, sym_name = "s30", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 31 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 32 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 33 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 35 : i64, operator_kind = 7 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 36 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 37 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 38 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 39 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
        }

        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 40 : i64, procedure_kind = 2 : i32, sym_name = "s40", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 41 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 42 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 43 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 44 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 45 : i64, operator_kind = 8 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 46 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 47 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 48 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 49 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
        }

        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 50 : i64, procedure_kind = 2 : i32, sym_name = "s50", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 51 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 52 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 53 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 54 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 55 : i64, operator_kind = 9 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 56 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 57 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 58 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 59 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

// Weak until vacuously completes every still-live attempt at EOS.
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_eos_count.11.until_weak(
// CHECK-SAME: obelisk_sim.concurrent_eos_counted
// CHECK: obelisk_sim.ref.load
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.persistent_until_aggregate_tokens
// CHECK-SAME: obelisk_sim.persistent_until_kind = "until"
// CHECK-SAME: obelisk_sim.persistent_until_monitor
// CHECK-SAME: obelisk_sim.sva_transition_normal_form = "canonical-minimal"
// One shared aggregate counter is captured by the EOS coordinator and carried
// across suspension; no per-attempt process or bitset is allocated.
// CHECK: [[LIVE_REF:%.*]] = obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_0.$concurrent_eos_count.11.until_weak
// CHECK: cf.br ^[[WAIT:bb[0-9]+]]([[LIVE_REF]] : !obelisk_sim.ref<i64>)
// CHECK: ^[[WAIT]]({{%.*}}: !obelisk_sim.ref<i64>):
// CHECK: obelisk_sim.suspend.edge {{.*}} to ^[[SAMPLE:bb[0-9]+]]({{%.*}} : !obelisk_sim.ref<i64>)
// CHECK: ^[[SAMPLE]]([[LIVE_REF_ARG:%.*]]: !obelisk_sim.ref<i64>):
// CHECK: [[A:%.*]] = obelisk_sim.assert.sampled_read
// CHECK: [[B:%.*]] = obelisk_sim.assert.sampled_read
// CHECK: [[LIVE:%.*]] = obelisk_sim.ref.load [[LIVE_REF_ARG]]
// CHECK: [[ATTEMPTS:%.*]] = arith.addi [[LIVE]],
// CHECK: [[NEXT:%.*]] = arith.select {{%.*}}, [[ATTEMPTS]], {{%.*}} : i64
// CHECK: obelisk_sim.ref.store [[NEXT]] to [[LIVE_REF_ARG]]
// CHECK: arith.subi
// CHECK: cf.cond_br {{%.*}}, {{.*}}, ^[[WAIT]]([[LIVE_REF_ARG]] : !obelisk_sim.ref<i64>)
// CHECK: arith.subi

// CHECK-LABEL: obelisk_sim.func private @unit_1.$concurrent_eos_count.31.until_strong(
// CHECK-SAME: obelisk_sim.concurrent_eos_counted
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.persistent_until_kind = "s_until"
// CHECK-SAME: obelisk_sim.persistent_until_strong

// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.persistent_until_inclusive
// CHECK-SAME: obelisk_sim.persistent_until_kind = "until_with"

// CHECK-LABEL: obelisk_sim.func private @unit_3.$concurrent_eos_count.51.until_strong(
// CHECK-SAME: obelisk_sim.concurrent_eos_counted
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: obelisk_sim.persistent_until_inclusive
// CHECK-SAME: obelisk_sim.persistent_until_kind = "s_until_with"
// CHECK-SAME: obelisk_sim.persistent_until_strong
