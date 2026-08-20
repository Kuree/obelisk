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

        // In an assertion the unqualified sequence operand is weak. Negation
        // makes the outer property strong and reverses each live result.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 2 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 11 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 12 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.disable_iff attributes {node_id = 15 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "top.rst", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.rst, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.unary attributes {has_range = false, node_id = 17 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
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
            obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 30 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "not-branch-pass", node_id = 31 : i64, semantic_type = !obelisk.ranged_packed_array<119 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }

        // A strong branching operand fails when pending. Its negation is a
        // weak nonvacuous success, including for cover property.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 40 : i64, procedure_kind = 2 : i32, sym_name = "s40", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 41 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 42 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 43 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 44 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 45 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.strong_weak attributes {node_id = 46 : i64, strength = 0 : i32} {
                  obelisk.sv.assertion.binary attributes {node_id = 47 : i64, operator_kind = 1 : i32} {
                    obelisk.sv.assertion.first_match attributes {match_item_count = 0 : i64, node_id = 48 : i64} {
                      obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 49 : i64} {
                        obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 50 : i64, repetition_is_unbounded = false} {
                          obelisk.sv.expression.named_value attributes {node_id = 51 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                        obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 52 : i64, repetition_is_unbounded = false} {
                          obelisk.sv.expression.named_value attributes {node_id = 53 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                          }
                        }
                      }
                    }
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 54 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 55 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 56 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 57 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 58 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 59 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 60 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "cover-not-strong-branch-pass", node_id = 61 : i64, semantic_type = !obelisk.ranged_packed_array<223 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }

        // Explicit strength keeps a one-cycle Boolean branch on the temporal
        // inversion route, but no attempt can remain pending at EOS.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 70 : i64, procedure_kind = 2 : i32, sym_name = "s70", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 71 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 72 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 73 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 74 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 75 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.strong_weak attributes {node_id = 76 : i64, strength = 0 : i32} {
                  obelisk.sv.assertion.binary attributes {node_id = 77 : i64, operator_kind = 1 : i32} {
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 78 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 79 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 80 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 81 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 82 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 83 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "not-strong-boolean-branch-pass", node_id = 84 : i64, semantic_type = !obelisk.ranged_packed_array<239 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// The weak operand becomes a strong outer property. EOS unions its two branch
// words and reports each live start age once through the default failure path.
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_cancel.11(
// CHECK-COUNT-3: obelisk_sim.ref.store
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_eos_report.11.strong(
// CHECK-SAME: !obelisk_sim.ref<i64>
// CHECK-SAME: i64
// CHECK: concurrent assertion failed
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_eos.11.strong(
// CHECK: obelisk_sim.ref.load
// CHECK: obelisk_sim.ref.load
// CHECK: arith.ori
// CHECK-COUNT-2: obelisk_sim.spawn @unit_0.$concurrent_eos_report.11.strong
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK: obelisk_sim.spawn @unit_0.$concurrent_cancel.11
// CHECK: obelisk_sim.spawn @unit_0.$concurrent_eos.11.strong
// The generated start/age failure checks all select the outer pass action;
// the age-one check is unreachable while the longer alternative continues.
// CHECK-COUNT-3: obelisk_sim.spawn @unit_0.fork.11.0.0

// Nested first_match contributes two of three strong operand alternatives.
// Its pending failure is inverted into one nonvacuous cover hit per live age.
// CHECK-LABEL: obelisk_sim.func private @unit_1.$concurrent_eos_report.41.weak(
// CHECK: obelisk_sim.bytes.constant "cover-not-strong-branch-pass"
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
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK: obelisk_sim.spawn @unit_1.$concurrent_eos.41.weak

// Explicitly strong one-cycle alternatives still invert live results, while
// allocating neither state nor a final-phase coordinator.
// CHECK-NOT: @unit_2.$concurrent_eos
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_2.fork.71.0.0
// CHECK-NOT: $concurrent_eos
