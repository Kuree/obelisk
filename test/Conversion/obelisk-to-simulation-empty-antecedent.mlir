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
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.c", name = "c", node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s11.c"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s12.c"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 13 : i64, procedure_kind = 2 : i32, sym_name = "s13", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 14 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 15 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 16 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 18 : i64, operator_kind = 12 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 19 : i64, repetition_is_unbounded = false, repetition_kind = 0 : i32, repetition_max = 0 : i64, repetition_min = 0 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 20 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 21 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 24 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "empty-imp-pass", is_signed = false, node_id = 25 : i64, semantic_type = !obelisk.ranged_packed_array<111 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 26 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 27 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "empty-imp-fail", is_signed = false, node_id = 28 : i64, semantic_type = !obelisk.ranged_packed_array<111 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 29 : i64, procedure_kind = 2 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 30 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 31 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 32 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 33 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 34 : i64, operator_kind = 14 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 35 : i64, repetition_is_unbounded = false, repetition_kind = 0 : i32, repetition_max = 0 : i64, repetition_min = 0 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 36 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 37 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 38 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 39 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 40 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 41 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 42 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 43 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "empty-follow-hit", is_signed = false, node_id = 44 : i64, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 45 : i64, procedure_kind = 2 : i32, sym_name = "s15", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 46 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 47 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 48 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 49 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 50 : i64, operator_kind = 11 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 51 : i64, repetition_is_unbounded = false, repetition_kind = 0 : i32, repetition_max = 1 : i64, repetition_min = 0 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 52 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 53 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 54 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 55 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 56 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "mixed-overlap-pass", is_signed = false, node_id = 57 : i64, semantic_type = !obelisk.ranged_packed_array<143 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 58 : i64, procedure_kind = 2 : i32, sym_name = "s16", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 59 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 60 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 61 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 62 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 63 : i64, operator_kind = 13 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 64 : i64, repetition_is_unbounded = false, repetition_kind = 0 : i32, repetition_max = 1 : i64, repetition_min = 0 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 65 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 66 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 67 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 68 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 69 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "mixed-overlap-hit", is_signed = false, node_id = 70 : i64, semantic_type = !obelisk.ranged_packed_array<135 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 71 : i64, procedure_kind = 2 : i32, sym_name = "s17", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 72 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 73 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 74 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 75 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 76 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.binary attributes {node_id = 77 : i64, operator_kind = 12 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 78 : i64, repetition_is_unbounded = false, repetition_kind = 0 : i32, repetition_max = 0 : i64, repetition_min = 0 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 79 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 80 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 81 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 82 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 83 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "empty-not-hit", is_signed = false, node_id = 84 : i64, semantic_type = !obelisk.ranged_packed_array<103 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// An empty-only |=> antecedent is one guaranteed match whose consequent starts
// on this clock. The repeated operand a is never sampled; b alone selects one
// pass or fail action, and no handoff/final state exists.
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.empty_antecedent_nonoverlap
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK: %[[EMPTY_IMP_SAMPLE:.*]] = obelisk_sim.assert.sampled_read {{%.*}} from %arg3
// CHECK-NEXT: %[[EMPTY_IMP_TRUE:.*]] = obelisk_sim.logic.is_true %[[EMPTY_IMP_SAMPLE]]
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_0.fork.14.1.1
// CHECK: obelisk_sim.spawn @unit_0.fork.14.0.0

// Empty #=# likewise starts the two-age b ##1 c consequent immediately. The
// weak assert completion owns one state cell and reports each pending attempt
// once at EOS; there is no extra nonoverlap handoff age.
// CHECK-LABEL: obelisk_sim.func private @unit_1.$concurrent_eos_report.30.weak(
// CHECK: obelisk_sim.bytes.constant "empty-follow-hit"
// CHECK-LABEL: obelisk_sim.func private @unit_1.$concurrent_eos.30.weak(
// CHECK-COUNT-1: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_1.$concurrent_eos_report.30.weak
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.empty_antecedent_nonoverlap
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.followed_by_monitor
// CHECK-SAME: obelisk_sim.strong_weak_monitor
// CHECK-COUNT-1: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_1.$concurrent_eos.30.weak
// CHECK: obelisk_sim.assert.sampled_read {{%.*}} from %arg4
// CHECK: obelisk_sim.assert.sampled_read {{%.*}} from %arg3
// CHECK-NOT: obelisk_sim.assert.sampled_read

// For overlapped implication, the empty branch of a[*0:1] has no endpoint and
// is discarded. The retained a endpoint evaluates c on the same clock. False
// a selects the vacuous pass, while true a selects exactly one pass or failure.
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.overlapped_empty_matches_ignored
// CHECK-NOT: obelisk_sim.branching_antecedent_monitor
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.assert.sampled_read {{%.*}} from %arg2
// CHECK: obelisk_sim.spawn @unit_2.fork.46.0.0
// CHECK: obelisk_sim.assert.sampled_read {{%.*}} from %arg3
// CHECK: obelisk_sim.spawn @unit_2.fork.46.1.2
// CHECK: obelisk_sim.spawn @unit_2.fork.46.0.0
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.spawn @unit_2.fork.46

// The same endpoint rule applies to overlapped followed-by, but no-match a is
// a failure and therefore cannot produce the cover hit.
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: obelisk_sim.followed_by_monitor
// CHECK-SAME: obelisk_sim.overlapped_empty_matches_ignored
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.assert.sampled_read {{%.*}} from %arg2
// CHECK: obelisk_sim.assert.sampled_read {{%.*}} from %arg3
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-COUNT-1: obelisk_sim.spawn @unit_3.fork.59.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_3.fork.59.0.0

// Temporal not retains the empty-antecedent current-clock rewrite, samples b
// only, and inverts its one result so false b produces the cover hit.
// CHECK-LABEL: obelisk_sim.func private @unit_4(
// CHECK-SAME: obelisk_sim.empty_antecedent_nonoverlap
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.assert.sampled_read {{%.*}} from %arg3
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-COUNT-1: obelisk_sim.spawn @unit_4.fork.72.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_4.fork.72.0.0
