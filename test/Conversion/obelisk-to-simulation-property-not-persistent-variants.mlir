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
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 11 : i64, procedure_kind = 2 : i32, sym_name = "s11", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 12 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 13 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 14 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 16 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.binary attributes {node_id = 17 : i64, operator_kind = 8 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 18 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 20 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 22 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 23 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u0-pass", is_signed = false, node_id = 24 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 25 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 26 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u0-fail", is_signed = false, node_id = 27 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 28 : i64, procedure_kind = 2 : i32, sym_name = "s12", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 29 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 30 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 31 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 32 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 33 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.binary attributes {node_id = 34 : i64, operator_kind = 9 : i32} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 35 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 36 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 37 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 38 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 39 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 40 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u1-hit", is_signed = false, node_id = 41 : i64, semantic_type = !obelisk.ranged_packed_array<47 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 42 : i64, procedure_kind = 2 : i32, sym_name = "s13", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = true, node_id = 43 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 44 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 45 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 46 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 47 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 48 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 49 : i64, repetition_is_unbounded = true, repetition_kind = 2 : i32, repetition_min = 2 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 50 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 51 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 52 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 53 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 54 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u2-pass", is_signed = false, node_id = 55 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 56 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 57 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u2-fail", is_signed = false, node_id = 58 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 59 : i64, procedure_kind = 2 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 60 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 61 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 62 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 63 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.unary attributes {has_range = false, node_id = 64 : i64, operator_kind = 0 : i32, range_is_unbounded = false} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 65 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 66 : i64, repetition_is_unbounded = true, repetition_kind = 1 : i32, repetition_min = 2 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 67 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 68 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 69 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 70 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$info", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 71 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "u3-hit", is_signed = false, node_id = 72 : i64, semantic_type = !obelisk.ranged_packed_array<47 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// Inclusive weak until retains its distinct transition equation. Negation
// swaps both live callbacks, and its weak EOS success becomes outer failure.
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_eos_count.12.until_weak(
// CHECK: obelisk_sim.spawn @unit_0.fork.12.1.1
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.persistent_until_inclusive
// CHECK-SAME: obelisk_sim.persistent_until_kind = "until_with"
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK: obelisk_sim.spawn @unit_0.$concurrent_eos_count.12.until_weak
// CHECK: obelisk_sim.spawn @unit_0.fork.12.1.1
// CHECK: obelisk_sim.spawn @unit_0.fork.12.0.0

// Inclusive strong until under cover has strong-failure EOS and live failure
// counts inverted into hits, while operand successes remain silent.
// CHECK-LABEL: obelisk_sim.func private @unit_1.$concurrent_eos_count.29.until_strong(
// CHECK: obelisk_sim.spawn @unit_1.fork.29.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.persistent_until_inclusive
// CHECK-SAME: obelisk_sim.persistent_until_kind = "s_until_with"
// CHECK-SAME: obelisk_sim.persistent_until_strong
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK: obelisk_sim.spawn @unit_1.$concurrent_eos_count.29.until_strong
// CHECK: obelisk_sim.spawn @unit_1.fork.29.0.0
// CHECK-NOT: obelisk_sim.spawn @unit_1.fork

// Goto repetition plus a terminal continuation uses four distinct aggregate
// DFA cells. Every weak EOS completion and terminal success becomes failure
// after negation; an X/Z gap becomes a live pass, and the EOS coordinator
// counts all four cells.
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_eos_count.43.repetition_weak(
// CHECK-COUNT-4: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_2.fork.43.1.1
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "goto"
// CHECK-SAME: obelisk_sim.persistent_repetition_min = 2 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 4 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_unbounded
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK-COUNT-4: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_2.$concurrent_eos_count.43.repetition_weak
// CHECK: obelisk_sim.spawn @unit_2.fork.43.1.1
// CHECK: obelisk_sim.spawn @unit_2.fork.43.0.0

// Nonconsecutive repetition has a different three-cell DFA. Cover's strong
// operand failure at EOS becomes one hit per counted attempt; terminal
// successes invert to silent failures, while an X/Z gap is an operand failure
// and therefore produces the negated property's live pass.
// CHECK-LABEL: obelisk_sim.func private @unit_3.$concurrent_eos_count.60.repetition_strong(
// CHECK-COUNT-3: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_3.fork.60.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "weak"
// CHECK-SAME: obelisk_sim.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "nonconsecutive"
// CHECK-SAME: obelisk_sim.persistent_repetition_min = 2 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 3 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_unbounded
// CHECK-SAME: obelisk_sim.temporal_property_negation
// CHECK-COUNT-3: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_3.$concurrent_eos_count.60.repetition_strong
// CHECK: obelisk_sim.spawn @unit_3.fork.60.0.0
