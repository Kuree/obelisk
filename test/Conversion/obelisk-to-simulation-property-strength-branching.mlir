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
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.rst", lifetime = 1 : i32, name = "rst", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.rst"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.b"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.c"} {
        }

        // The two alternatives can carry the same property attempt at the
        // same age. Strong EOS completion must report that attempt once.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 2 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 11 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 12 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.disable_iff attributes {node_id = 15 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "top.rst", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.rst, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.strong_weak attributes {node_id = 17 : i64, strength = 0 : i32} {
                  obelisk.sv.assertion.binary attributes {node_id = 18 : i64, operator_kind = 1 : i32} {
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 19 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 20 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 22 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 24 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 25 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 26 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 27 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 28 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }

        // Weak qualification completes every still-live property attempt with
        // one vacuous success, despite duplication across alternatives.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 40 : i64, procedure_kind = 2 : i32, sym_name = "s40", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 41 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 42 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 43 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 44 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.strong_weak attributes {node_id = 45 : i64, strength = 1 : i32} {
                obelisk.sv.assertion.binary attributes {node_id = 60 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.first_match attributes {match_item_count = 0 : i64, node_id = 46 : i64} {
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 47 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 48 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 49 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 50 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 51 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 52 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 53 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 54 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 55 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 56 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 57 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 58 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "weak-branch-pass", node_id = 59 : i64, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }

        // Weak completion is vacuous and therefore cannot create a cover hit.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 70 : i64, procedure_kind = 2 : i32, sym_name = "s70", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 71 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 72 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 73 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 74 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.strong_weak attributes {node_id = 75 : i64, strength = 1 : i32} {
                obelisk.sv.assertion.binary attributes {node_id = 76 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 77 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 78 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 79 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 80 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 81 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 82 : i64} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 83 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 84 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 85 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 86 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 87 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 88 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "cover-weak-branch-pass", node_id = 89 : i64, semantic_type = !obelisk.ranged_packed_array<175 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }

        // An all-one-cycle branching sequence is still explicitly strong,
        // but no attempt can remain pending and no EOS state is needed.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 100 : i64, procedure_kind = 2 : i32, sym_name = "s100", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 101 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 102 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 103 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 104 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.strong_weak attributes {node_id = 105 : i64, strength = 0 : i32} {
                obelisk.sv.assertion.binary attributes {node_id = 106 : i64, operator_kind = 1 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 107 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 108 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 109 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 110 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

// Strong branching EOS unions the two alternative state words. The two
// possible live ages produce two report sites, not one site per alternative.
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_cancel.11(
// Two alternative states plus one disable epoch are cleared/advanced.
// CHECK-COUNT-3: obelisk_sim.ref.store
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_eos_report.11.strong(
// CHECK-SAME: %[[EPOCH_REF:arg[0-9]+]]: !obelisk_sim.ref<i64>
// CHECK-SAME: %[[EXPECTED:arg[0-9]+]]: i64
// CHECK: %[[EPOCH:.*]] = obelisk_sim.ref.load %[[EPOCH_REF]]
// CHECK: arith.cmpi eq, %[[EPOCH]], %[[EXPECTED]]
// CHECK: concurrent assertion failed
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_eos.11.strong(
// CHECK-COUNT-2: obelisk_sim.ref.load
// CHECK: arith.ori
// CHECK-COUNT-2: obelisk_sim.spawn @unit_0.$concurrent_eos_report.11.strong
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.branching_sequence_monitor
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.strong_weak_monitor
// CHECK: obelisk_sim.spawn @unit_0.$concurrent_cancel.11
// CHECK: obelisk_sim.spawn @unit_0.$concurrent_eos.11.strong

// Weak assertion EOS dispatches one vacuous pass for each live start age.
// CHECK-LABEL: obelisk_sim.func private @unit_1.$concurrent_eos_report.41.weak(
// CHECK: obelisk_sim.bytes.constant "weak-branch-pass"
// CHECK-LABEL: obelisk_sim.func private @unit_1.$concurrent_eos.41.weak(
// CHECK: obelisk_sim.ref.load
// CHECK: obelisk_sim.ref.load
// CHECK: arith.ori
// CHECK: obelisk_sim.ref.load
// CHECK: arith.ori
// CHECK-COUNT-2: obelisk_sim.spawn @unit_1.$concurrent_eos_report.41.weak
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 3 : i64
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.first_match_monitor
// CHECK-SAME: obelisk_sim.first_match_priority_groups = 1 : i64
// CHECK-SAME: obelisk_sim.strong_weak_monitor
// CHECK: obelisk_sim.spawn @unit_1.$concurrent_eos.41.weak

// Weak cover-property completion is vacuous, so no EOS actor is created.
// CHECK-NOT: @unit_2.$concurrent_eos
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.strong_weak_monitor
// CHECK-NOT: $concurrent_eos

// One-cycle alternatives retain strength metadata but allocate no monitor
// state and outline no EOS coordinator.
// CHECK-NOT: @unit_3.$concurrent_eos
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.strong_weak_monitor
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK-NOT: $concurrent_eos
