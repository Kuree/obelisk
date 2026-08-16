// REQUIRES: z3
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' -o %t.threaded.mlir
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' --mlir-disable-threading -o %t.serial.mlir
// RUN: diff %t.threaded.mlir %t.serial.mlir
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
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
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 8 : i64, procedure_kind = 2 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 9 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 10 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 11 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 13 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 14 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 100 : i64, procedure_kind = 2 : i32, sym_name = "s100", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 16 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 17 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 18 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 20 : i64, operator_kind = 5 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 21 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 23 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 24 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 101 : i64, procedure_kind = 2 : i32, sym_name = "s101", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 25 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 26 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 27 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 28 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 29 : i64, operator_kind = 10 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 30 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 32 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 33 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 102 : i64, procedure_kind = 2 : i32, sym_name = "s102", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 34 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 35 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 36 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 37 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.conditional attributes {has_else = true, node_id = 38 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 39 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 40 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 41 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 42 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 43 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 103 : i64, procedure_kind = 2 : i32, sym_name = "s103", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 44 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 45 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 46 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 47 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.conditional attributes {has_else = true, node_id = 48 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 49 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 50 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 51 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 52 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 53 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 54 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 104 : i64, procedure_kind = 2 : i32, sym_name = "s104", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 55 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 56 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 57 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 58 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.conditional attributes {has_else = false, node_id = 59 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 60 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 61 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 62 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 63 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 64 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 65 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 66 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 105 : i64, procedure_kind = 2 : i32, sym_name = "s105", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 67 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 68 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 69 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 70 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.case attributes {has_default = true, item_group_sizes = [1], node_id = 71 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 72 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", node_id = 73 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 74 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 75 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 76 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 78 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 106 : i64, procedure_kind = 2 : i32, sym_name = "s106", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 79 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 80 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 81 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 82 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.case attributes {has_default = false, item_group_sizes = [1], node_id = 83 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 84 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", node_id = 85 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 86 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 87 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 88 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 89 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 90 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 91 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// `not a` reports failure exactly when sampled `a` is true.
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: home_region = 8 : i32
// CHECK: [[A0:%.*]] = obelisk_sim.assert.sampled_read
// CHECK: [[A:%.*]] = obelisk_sim.logic.is_true [[A0]]
// CHECK: cf.cond_br [[A]],

// `a iff b` expands to `(a && b) || (!a && !b)`.
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 2 : i64
// CHECK-COUNT-2: obelisk_sim.assert.sampled_read
// CHECK: arith.xori
// CHECK: arith.xori
// CHECK: arith.ori

// `a implies b` expands to `!a || b`.
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 2 : i64
// CHECK: obelisk_sim.assert.sampled_read
// CHECK: arith.xori
// CHECK: obelisk_sim.assert.sampled_read
// CHECK: arith.ori
// CHECK-NOT: obelisk.sv.assertion

// Z3 proves `(a && b) || (!a && b)` equivalent to `b` before monitor SSA is
// built, removing one sampled read and the branching state entirely.
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_after = 1 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_before = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_literals_after = 1 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_literals_before = 4 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_solver = "z3"
// CHECK-COUNT-1: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.branching_sequence_monitor

// A non-equivalent else branch keeps both guarded alternatives.
// CHECK-LABEL: obelisk_sim.func private @unit_4(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_after = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_before = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_solver = "z3"

// A missing else is a vacuous success when the condition is false. Cover
// property tracks that alternative separately and does not count it as a hit.
// CHECK-LABEL: obelisk_sim.func private @unit_5(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.vacuous_sequence_alternatives = 1 : i64
// CHECK: [[B_BITS:%.*]] = obelisk_sim.assert.sampled_read
// CHECK: [[B_TRUTH:%.*]] = obelisk_sim.logic.is_true [[B_BITS]]
// CHECK: [[A_BITS:%.*]] = obelisk_sim.assert.sampled_read
// CHECK: [[A_TRUTH:%.*]] = obelisk_sim.logic.is_true [[A_BITS]]
// CHECK: [[NONVACUOUS_HIT:%.*]] = arith.andi [[B_TRUTH]], [[A_TRUTH]]
// CHECK: cf.cond_br [[NONVACUOUS_HIT]],

// Z3 proves that both the matching and default case arms require the same
// predicate, eliminating the selector and its four-state equality guard.
// CHECK-LABEL: obelisk_sim.func private @unit_6(
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_after = 1 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_alternatives_before = 2 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_literals_after = 1 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_literals_before = 4 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_solver = "z3"
// CHECK-COUNT-1: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.logic.compare case_eq

// Without a default, no matching label is vacuous and cannot create a cover
// hit. The surviving hit requires both the case-equality guard and body.
// CHECK-LABEL: obelisk_sim.func private @unit_7(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.vacuous_sequence_alternatives = 1 : i64
// CHECK: [[CASE_BODY:%.*]] = obelisk_sim.logic.is_true
// CHECK: [[CASE_MATCH:%.*]] = obelisk_sim.logic.compare case_eq
// CHECK: [[CASE_HIT:%.*]] = arith.andi [[CASE_BODY]], [[CASE_MATCH]]
// CHECK: cf.cond_br [[CASE_HIT]],
