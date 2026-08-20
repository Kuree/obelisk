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

        // a[*0:$] ##1 a has one saturated DFA state. Its zero-occurrence
        // endpoint tests a on the first clock; bare assert completion is weak.
        // The repeated and terminal roles share one same-snapshot read.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 2 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 11 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 12 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 15 : i64} {
                obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 16 : i64, repetition_is_unbounded = true, repetition_kind = 0 : i32, repetition_min = 0 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 18 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 20 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 21 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "zero-pass", node_id = 22 : i64, semantic_type = !obelisk.ranged_packed_array<71 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }

        // A deterministic prefix delays entry into the same one-state DFA.
        // Explicit strong completion fails every surviving token at EOS.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 30 : i64, procedure_kind = 2 : i32, sym_name = "s30", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 31 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 32 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 33 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.strong_weak attributes {node_id = 35 : i64, strength = 0 : i32} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 36 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 37 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 38 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 39 : i64, repetition_is_unbounded = true, repetition_kind = 0 : i32, repetition_min = 0 : i64} {
                    obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 41 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 42 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
          }
        }

        // a[->0:$] ##1 b starts in the saturated terminal-pending state.
        // A false b permits a to become the last occurrence; a true b also
        // completes the zero-occurrence endpoint on the entry clock.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 50 : i64, procedure_kind = 2 : i32, sym_name = "s50", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 51 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 52 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 53 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 54 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 55 : i64} {
                obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 56 : i64, repetition_is_unbounded = true, repetition_kind = 2 : i32, repetition_min = 0 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 57 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 58 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 59 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 60 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 61 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "goto-zero-pass", node_id = 62 : i64, semantic_type = !obelisk.ranged_packed_array<111 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }

        // a[=0:$] ##1 b has one saturated eligible state. Since the maximum
        // is unbounded, true and false a have the same saturated destination,
        // but X/Z a still kills the trace. The terminal b is tested on every
        // clock, including the zero-occurrence entry clock.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 70 : i64, procedure_kind = 2 : i32, sym_name = "s70", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 71 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 72 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 73 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 74 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 75 : i64} {
                obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 76 : i64, repetition_is_unbounded = true, repetition_kind = 1 : i32, repetition_min = 0 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 77 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 78 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 79 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 80 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 81 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "nonconsecutive-zero-pass", node_id = 82 : i64, semantic_type = !obelisk.ranged_packed_array<199 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// The zero minimum collapses to exactly one saturated pending-terminal state.
// The weak EOS coordinator counts that state and dispatches the pass action.
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_eos_count.11.repetition_weak(
// CHECK-COUNT-1: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_0.fork.11.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "consecutive"
// CHECK-SAME: obelisk_sim.persistent_repetition_min = 0 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 1 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_unbounded
// CHECK-COUNT-1: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK-COUNT-2: arith.select
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_0.fork.11.0.0

// A one-cycle prefix adds one bitset but does not add repetition DFA states.
// Strong EOS completion counts both the pending token and prefix bitset as
// failures; the prefix and zero-occurrence terminal are sampled separately.
// CHECK-LABEL: obelisk_sim.func private @unit_1.$concurrent_eos_count.31.repetition_strong(
// CHECK-COUNT-2: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "consecutive"
// CHECK-SAME: obelisk_sim.persistent_repetition_min = 0 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 1 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_unbounded
// CHECK: obelisk_sim.spawn @unit_1.$concurrent_eos_count.31.repetition_strong
// CHECK-COUNT-3: obelisk_sim.assert.sampled_read
// CHECK-NOT: obelisk_sim.assert.sampled_read

// Goto minimum zero uses one nonpending and one terminal-pending saturated
// state. Both are weak-completed at EOS. The repeated term is split into
// true, strict-false, and X/Z classes; terminal failure gates the X/Z failure.
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_eos_count.51.repetition_weak(
// CHECK-COUNT-2: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_2.fork.51.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "goto"
// CHECK-SAME: obelisk_sim.persistent_repetition_min = 0 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 2 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_unbounded
// CHECK-COUNT-2: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: %[[GOTO_NONPENDING:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: %[[GOTO_PENDING:.*]] = obelisk_sim.ref.load
// CHECK: %[[GOTO_ONE:.*]] = arith.constant {{.*}}1 : i64
// CHECK-NEXT: %[[GOTO_ENTRY:.*]] = arith.addi %[[GOTO_PENDING]], %[[GOTO_ONE]] : i64
// CHECK: %[[GOTO_REPEAT:.*]] = obelisk_sim.assert.sampled_read
// CHECK-NEXT: %[[GOTO_TRUE:.*]] = obelisk_sim.logic.is_true %[[GOTO_REPEAT]]
// CHECK: %[[GOTO_ZERO:.*]] = obelisk_sim.logic.constant false, false
// CHECK-NEXT: %[[GOTO_FALSE:.*]] = obelisk_sim.logic.compare case_eq %[[GOTO_REPEAT]], %[[GOTO_ZERO]]
// CHECK-NEXT: %[[GOTO_KNOWN:.*]] = arith.ori %[[GOTO_TRUE]], %[[GOTO_FALSE]]
// CHECK: %[[GOTO_UNKNOWN:.*]] = arith.xori %[[GOTO_KNOWN]],
// CHECK: %[[GOTO_TERMINAL:.*]] = obelisk_sim.assert.sampled_read
// CHECK: %[[GOTO_NOT_TERMINAL:.*]] = arith.xori
// CHECK: arith.andi %[[GOTO_NOT_TERMINAL]], %[[GOTO_UNKNOWN]]
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_2.fork.51.0.0
// CHECK: obelisk_sim.spawn @unit_2.fork.51.1.2

// Nonconsecutive minimum zero has one saturated eligible state. Its terminal
// is evaluated immediately. True and strict-false repeated values retain the
// state after terminal failure; X/Z instead dispatches assertion failure.
// CHECK-LABEL: obelisk_sim.func private @unit_3.$concurrent_eos_count.71.repetition_weak(
// CHECK-COUNT-1: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_3.fork.71.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_3(
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "nonconsecutive"
// CHECK-SAME: obelisk_sim.persistent_repetition_min = 0 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 1 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_unbounded
// CHECK-COUNT-1: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: %[[NONCONSECUTIVE_ACTIVE:.*]] = obelisk_sim.ref.load
// CHECK: %[[NONCONSECUTIVE_ONE:.*]] = arith.constant {{.*}}1 : i64
// CHECK-NEXT: %[[NONCONSECUTIVE_ENTRY:.*]] = arith.addi %[[NONCONSECUTIVE_ACTIVE]], %[[NONCONSECUTIVE_ONE]] : i64
// CHECK: %[[NONCONSECUTIVE_REPEAT:.*]] = obelisk_sim.assert.sampled_read
// CHECK-NEXT: %[[NONCONSECUTIVE_TRUE:.*]] = obelisk_sim.logic.is_true %[[NONCONSECUTIVE_REPEAT]]
// CHECK: %[[NONCONSECUTIVE_ZERO:.*]] = obelisk_sim.logic.constant false, false
// CHECK-NEXT: %[[NONCONSECUTIVE_FALSE:.*]] = obelisk_sim.logic.compare case_eq %[[NONCONSECUTIVE_REPEAT]], %[[NONCONSECUTIVE_ZERO]]
// CHECK-NEXT: %[[NONCONSECUTIVE_KNOWN:.*]] = arith.ori %[[NONCONSECUTIVE_TRUE]], %[[NONCONSECUTIVE_FALSE]]
// CHECK: %[[NONCONSECUTIVE_UNKNOWN:.*]] = arith.xori %[[NONCONSECUTIVE_KNOWN]],
// CHECK: %[[NONCONSECUTIVE_TERMINAL:.*]] = obelisk_sim.assert.sampled_read
// CHECK: %[[NONCONSECUTIVE_NOT_TERMINAL:.*]] = arith.xori
// CHECK: arith.andi %[[NONCONSECUTIVE_NOT_TERMINAL]], %[[NONCONSECUTIVE_UNKNOWN]]
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_3.fork.71.0.0
// CHECK: obelisk_sim.spawn @unit_3.fork.71.1.2
