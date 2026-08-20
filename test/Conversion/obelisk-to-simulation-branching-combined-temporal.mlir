// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// REQUIRES: z3

// Combined temporal branching retains one compact state word for every
// post-minimization antecedent/consequent pair.  Each consequent obligation
// succeeds when any of its alternatives succeeds, cancels that obligation's
// remaining alternatives, and is then coalesced by original source attempt.

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "combined_temporal", name = "combined_temporal", node_id = 0 : i64, sym_name = "s0.combined_temporal"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "combined_temporal", is_uninstantiated = false, name = "combined_temporal", node_id = 3 : i64, referenced_path = "combined_temporal", referenced_symbol = @s0.combined_temporal, sym_name = "s3.combined_temporal"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "combined_temporal", name = "combined_temporal", node_id = 4 : i64, sym_name = "s4.combined_temporal", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "combined_temporal.clk", name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "combined_temporal.clk", lifetime = 1 : i32, name = "clk", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.clk"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "combined_temporal.a", name = "a", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "combined_temporal.a", lifetime = 1 : i32, name = "a", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.a"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "combined_temporal.b", name = "b", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.b"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "combined_temporal.b", lifetime = 1 : i32, name = "b", node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s10.b"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "combined_temporal.c", name = "c", node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s11.c"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "combined_temporal.c", lifetime = 1 : i32, name = "c", node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s12.c"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "combined_temporal.d", name = "d", node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s13.d"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "combined_temporal.d", lifetime = 1 : i32, name = "d", node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s14.d"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "combined_temporal.e", name = "e", node_id = 15 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s15.e"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "combined_temporal.e", lifetime = 1 : i32, name = "e", node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s16.e"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "combined_temporal", node_id = 17 : i64, procedure_kind = 2 : i32, sym_name = "s17", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 18 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 19 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 20 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "combined_temporal.clk", referenced_symbol = @s1.$root::@s3.combined_temporal::@s4.combined_temporal::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 22 : i64, operator_kind = 11 : i32} {
                obelisk.sv.assertion.binary attributes {node_id = 23 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 24 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "combined_temporal.a", referenced_symbol = @s1.$root::@s3.combined_temporal::@s4.combined_temporal::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 26 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 27 : i64, referenced_path = "combined_temporal.b", referenced_symbol = @s1.$root::@s3.combined_temporal::@s4.combined_temporal::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 28 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 29 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 30 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 31 : i64, referenced_path = "combined_temporal.c", referenced_symbol = @s1.$root::@s3.combined_temporal::@s4.combined_temporal::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 32 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 33 : i64, referenced_path = "combined_temporal.d", referenced_symbol = @s1.$root::@s3.combined_temporal::@s4.combined_temporal::@s14.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 34 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 35 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 36 : i64, referenced_path = "combined_temporal.c", referenced_symbol = @s1.$root::@s3.combined_temporal::@s4.combined_temporal::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 37 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 38 : i64, referenced_path = "combined_temporal.e", referenced_symbol = @s1.$root::@s3.combined_temporal::@s4.combined_temporal::@s16.e, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 39 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 40 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.combined_temporal", system_scope_path = "combined_temporal", system_scope_symbol = @s1.$root::@s3.combined_temporal::@s4.combined_temporal} {
                obelisk.sv.expression.string_literal attributes {constant_value = "pass", is_signed = false, node_id = 41 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "combined_temporal", node_id = 42 : i64, procedure_kind = 2 : i32, sym_name = "s18", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 43 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 44 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 45 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 46 : i64, referenced_path = "combined_temporal.clk", referenced_symbol = @s1.$root::@s3.combined_temporal::@s4.combined_temporal::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 47 : i64, operator_kind = 14 : i32} {
                obelisk.sv.assertion.binary attributes {node_id = 48 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 49 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 50 : i64, referenced_path = "combined_temporal.a", referenced_symbol = @s1.$root::@s3.combined_temporal::@s4.combined_temporal::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 51 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 52 : i64, referenced_path = "combined_temporal.b", referenced_symbol = @s1.$root::@s3.combined_temporal::@s4.combined_temporal::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.assertion.binary attributes {node_id = 53 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 54 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 55 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 56 : i64, referenced_path = "combined_temporal.c", referenced_symbol = @s1.$root::@s3.combined_temporal::@s4.combined_temporal::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 57 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 58 : i64, referenced_path = "combined_temporal.d", referenced_symbol = @s1.$root::@s3.combined_temporal::@s4.combined_temporal::@s14.d, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 59 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 60 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 61 : i64, referenced_path = "combined_temporal.c", referenced_symbol = @s1.$root::@s3.combined_temporal::@s4.combined_temporal::@s12.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 62 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 63 : i64, referenced_path = "combined_temporal.e", referenced_symbol = @s1.$root::@s3.combined_temporal::@s4.combined_temporal::@s16.e, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 64 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 65 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.combined_temporal", system_scope_path = "combined_temporal", system_scope_symbol = @s1.$root::@s3.combined_temporal::@s4.combined_temporal} {
                obelisk.sv.expression.string_literal attributes {constant_value = "cover", is_signed = false, node_id = 66 : i64, semantic_type = !obelisk.ranged_packed_array<39 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// The weak assertion consequent owns four pair words plus matched history.
// Pending source ages 2 and 1 each produce one coalesced weak EOS pass.
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_eos_branch.
// CHECK-SAME: %arg5: !obelisk_sim.ref<i64>
// CHECK-SAME: ) attributes
// CHECK-SAME: obelisk_sim.branching_antecedent_eos_coalescer
// CHECK-COUNT-5: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_0.$concurrent_eos_branch_report.{{.*}}.pass{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 2 : i64
// CHECK: obelisk_sim.spawn @unit_0.$concurrent_eos_branch_report.{{.*}}.pass{{.*}}obelisk_sim.branching_antecedent_eos_source_age = 1 : i64
// CHECK-NOT: obelisk_sim.spawn @unit_0.$concurrent_eos_branch_report.

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.branching_antecedent_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_result_horizon = 3 : i64
// CHECK-SAME: obelisk_sim.branching_consequent_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.combined_bounded_branching_monitor
// CHECK-SAME: obelisk_sim.combined_bounded_branching_pairs = 4 : i64
// CHECK-SAME: obelisk_sim.combined_bounded_branching_pairs_before_minimization = 4 : i64
// CHECK-SAME: obelisk_sim.sva_boolean_antecedent_solver = "z3"
// CHECK-COUNT-5: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_0.$concurrent_eos_branch.
// CHECK-COUNT-4: obelisk_sim.branching_antecedent_consequent_trigger
// CHECK-NOT: obelisk_sim.branching_antecedent_consequent_trigger
// CHECK: obelisk_sim.branching_antecedent_universal_failure
// CHECK: obelisk_sim.branching_antecedent_universal_success
// CHECK-COUNT-4: obelisk_sim.branching_consequent_alternative_cancel
// CHECK-NOT: obelisk_sim.branching_consequent_alternative_cancel

// Followed-by uses the same four pair words in SSA-carried form and retains
// existential completion across the two antecedent match channels.
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.branching_antecedent_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_result_horizon = 4 : i64
// CHECK-SAME: obelisk_sim.branching_consequent_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.combined_bounded_branching_monitor
// CHECK-SAME: obelisk_sim.combined_bounded_branching_pairs = 4 : i64
// CHECK-SAME: obelisk_sim.followed_by_monitor
// CHECK: cf.br ^bb{{[0-9]+}}({{%.*}}, {{%.*}}, {{%.*}}, {{%.*}}, {{%.*}} : i64, i64, i64, i64, i64)
// CHECK: obelisk_sim.branching_antecedent_existential_success
// CHECK: obelisk_sim.branching_antecedent_existential_failure
// CHECK-COUNT-4: obelisk_sim.branching_consequent_alternative_cancel
// CHECK-NOT: obelisk_sim.branching_consequent_alternative_cancel
