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

        // a |-> (b or c)
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
              obelisk.sv.assertion.binary attributes {node_id = 35 : i64, operator_kind = 12 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 36 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 37 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 38 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 39 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 41 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 42 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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
// two ranged exact traces. Their state is returned on the monitor backedge.
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: obelisk_sim.branching_consequent_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_consequent_monitor
// CHECK-SAME: obelisk_sim.branching_consequent_nonoverlapped
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK: ^bb{{[0-9]+}}([[STATE0:%.*]]: i64, [[STATE1:%.*]]: i64):
// CHECK: [[A_SAMPLE_1:%.*]] = obelisk_sim.assert.sampled_read %arg0 from %arg2
// CHECK: [[A_MATCH_1:%.*]] = obelisk_sim.logic.is_true [[A_SAMPLE_1]]
// CHECK: [[STATE0_AGE0:%.*]] = arith.andi [[STATE0]], {{%.*}} : i64
// CHECK: [[ACTIVE0:%.*]] = arith.cmpi ne, [[STATE0_AGE0]], {{%.*}} : i64
// CHECK: [[B_SAMPLE_1:%.*]] = obelisk_sim.assert.sampled_read %arg0 from %arg3
// CHECK: [[B_MATCH_1:%.*]] = obelisk_sim.logic.is_true [[B_SAMPLE_1]]
// CHECK: [[ADVANCE0:%.*]] = arith.andi [[ACTIVE0]], [[B_MATCH_1]] : i1
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
// CHECK: cf.br {{.*}}([[NEXT0]], [[NEXT1]] : i64, i64) {{.*}}obelisk_sim.branching_consequent_backedge
// CHECK-NOT: obelisk.sv.assertion
