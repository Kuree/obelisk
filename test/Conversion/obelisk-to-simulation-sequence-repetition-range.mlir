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
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s14.b"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 2 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 8 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 9 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 10 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 12 : i64, repetition_is_unbounded = false, repetition_kind = 0 : i32, repetition_max = 3 : i64, repetition_min = 1 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 15 : i64, procedure_kind = 2 : i32, sym_name = "s15", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 16 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 17 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 18 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 20 : i64, operator_kind = 4 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 21 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 23 : i64, repetition_is_unbounded = false, repetition_kind = 0 : i32, repetition_max = 3 : i64, repetition_min = 3 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 24 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 25 : i64, procedure_kind = 2 : i32, sym_name = "s25", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 26 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 27 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 28 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 29 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.first_match attributes {match_item_count = 0 : i64, node_id = 30 : i64} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 3 : i64, min = 1 : i64}], node_id = 31 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 32 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 33 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 34 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 35 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 36 : i64, procedure_kind = 2 : i32, sym_name = "s36", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 37 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 38 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 39 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 41 : i64} {
                obelisk.sv.assertion.first_match attributes {match_item_count = 0 : i64, node_id = 42 : i64} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 43 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 44 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 45 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 46 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 47 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s14.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 48 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 49 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

// [*1:3] expands into the three exact traces a, a##1a, and a##1a##1a.
// The monitor shares one sampled read and keeps distinct endpoint state for
// all three alternatives.
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 3 : i64
// CHECK-SAME: obelisk_sim.branching_sequence_monitor
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK-COUNT-1: obelisk_sim.assert.sampled_read
// CHECK: arith.select
// CHECK: cf.cond_br

// `a within b[*3]` has three exact placements for a while preserving all
// three required b samples.
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 3 : i64
// CHECK-SAME: obelisk_sim.branching_sequence_monitor
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK-COUNT-2: obelisk_sim.assert.sampled_read
// CHECK: arith.select
// CHECK: cf.cond_br

// Top-level first_match shares the same bounded alternatives but records that
// later live endpoints are suppressed after the first successful endpoint.
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 3 : i64
// CHECK-SAME: obelisk_sim.branching_sequence_monitor
// CHECK-SAME: obelisk_sim.first_match_monitor
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK-COUNT-2: obelisk_sim.assert.sampled_read
// CHECK: arith.select
// CHECK: cf.cond_br

// A first_match nested before ##1 records the sub-sequence endpoint on every
// trace. The surviving priority gate cancels the later candidate when the
// earlier endpoint succeeds, while tautological gates fold away.
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_sequence_monitor
// CHECK-SAME: obelisk_sim.first_match_monitor
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK: arith.andi {{.*}}obelisk_sim.first_match_priority
// CHECK: cf.cond_br
// CHECK-NOT: obelisk.sv.assertion
