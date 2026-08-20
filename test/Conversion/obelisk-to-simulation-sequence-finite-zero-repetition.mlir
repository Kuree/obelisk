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

        // a[->0:2] ##1 b has nonpending count-0/1 states and terminal-pending
        // count-0/1/2 states. The empty endpoint tests b on the entry clock.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 2 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 11 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 12 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 15 : i64} {
                obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 16 : i64, repetition_is_unbounded = false, repetition_kind = 2 : i32, repetition_max = 2 : i64, repetition_min = 0 : i64} {
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
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 21 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "finite-goto-zero-pass", node_id = 22 : i64, semantic_type = !obelisk.ranged_packed_array<167 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }

        // strong(a[=0:2] ##1 c) has count-0/1/2 eligible states. A false c
        // permits known a to consume/wait, but count two rejects another a.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 30 : i64, procedure_kind = 2 : i32, sym_name = "s30", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 31 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 32 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 33 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.strong_weak attributes {node_id = 35 : i64, strength = 0 : i32} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 36 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 37 : i64, repetition_is_unbounded = false, repetition_kind = 1 : i32, repetition_max = 2 : i64, repetition_min = 0 : i64} {
                    obelisk.sv.expression.named_value attributes {node_id = 38 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 39 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
          }
        }

        // a[->0:0] ##1 a has only pending count zero: the terminal is tested
        // immediately, and terminal failure exhausts the exact-zero range.
        // Repeated and terminal roles share one semantic-symbol sample.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 50 : i64, procedure_kind = 2 : i32, sym_name = "s50", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 51 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 52 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 53 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 54 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 55 : i64} {
                obelisk.sv.assertion.simple attributes {has_repetition = true, is_null = false, node_id = 56 : i64, repetition_is_unbounded = false, repetition_kind = 2 : i32, repetition_max = 0 : i64, repetition_min = 0 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 57 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 58 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 59 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 60 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 61 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                obelisk.sv.expression.string_literal attributes {constant_value = "zero-range-pass", node_id = 62 : i64, semantic_type = !obelisk.ranged_packed_array<119 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// Goto [->0:2] owns exactly five cells. Weak EOS counts every survivor and
// dispatches the pass action once per token.
// CHECK-LABEL: obelisk_sim.func private @unit_0.$concurrent_eos_count.11.repetition_weak(
// CHECK-COUNT-5: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_0.fork.11.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "goto"
// CHECK-SAME: obelisk_sim.persistent_repetition_max = 2 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_min = 0 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 5 : i64
// CHECK-NOT: obelisk_sim.persistent_repetition_unbounded
// CHECK-COUNT-5: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_0.$concurrent_eos_count.11.repetition_weak
// CHECK: %[[GOTO_NONPENDING_0:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: %[[GOTO_NONPENDING_1:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: %[[GOTO_PENDING_0:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: %[[GOTO_PENDING_1:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: %[[GOTO_PENDING_2:.*]] = obelisk_sim.ref.load
// CHECK: %[[GOTO_ONE:.*]] = arith.constant {{.*}}1 : i64
// CHECK-NEXT: %[[GOTO_ENTRY:.*]] = arith.addi %[[GOTO_PENDING_0]], %[[GOTO_ONE]] : i64
// CHECK: %[[GOTO_REPEAT:.*]] = obelisk_sim.assert.sampled_read
// CHECK-NEXT: %[[GOTO_TRUE:.*]] = obelisk_sim.logic.is_true %[[GOTO_REPEAT]]
// CHECK: %[[GOTO_ZERO:.*]] = obelisk_sim.logic.constant false, false
// CHECK-NEXT: %[[GOTO_FALSE:.*]] = obelisk_sim.logic.compare case_eq %[[GOTO_REPEAT]], %[[GOTO_ZERO]]
// CHECK-NEXT: %[[GOTO_KNOWN:.*]] = arith.ori %[[GOTO_TRUE]], %[[GOTO_FALSE]]
// CHECK: %[[GOTO_UNKNOWN:.*]] = arith.xori %[[GOTO_KNOWN]],
// CHECK: %[[GOTO_TERMINAL_SAMPLE:.*]] = obelisk_sim.assert.sampled_read
// CHECK-NEXT: %[[GOTO_TERMINAL:.*]] = obelisk_sim.logic.is_true %[[GOTO_TERMINAL_SAMPLE]]
// CHECK: %[[GOTO_NOT_TERMINAL:.*]] = arith.xori %[[GOTO_TERMINAL]],
// CHECK: arith.select %[[GOTO_TERMINAL]], %[[GOTO_ENTRY]],
// CHECK: arith.andi %[[GOTO_NOT_TERMINAL]], %[[GOTO_UNKNOWN]]
// CHECK: arith.select %[[GOTO_TERMINAL]], %[[GOTO_PENDING_2]],
// CHECK: arith.select %[[GOTO_TERMINAL]], {{%.*}}, %[[GOTO_PENDING_2]]
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_0.fork.11.0.0
// CHECK: obelisk_sim.spawn @unit_0.fork.11.1.2

// Nonconsecutive [=0:2] owns count-0/1/2 cells. Strong EOS counts all three
// as failures. Count zero is immediately eligible, while the maximum state
// retains strict-false gaps and rejects true or unknown values.
// CHECK-LABEL: obelisk_sim.func private @unit_1.$concurrent_eos_count.31.repetition_strong(
// CHECK-COUNT-3: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_1.fork.31.0.2
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-SAME: obelisk_sim.end_of_simulation_strength = "strong"
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "nonconsecutive"
// CHECK-SAME: obelisk_sim.persistent_repetition_max = 2 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_min = 0 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 3 : i64
// CHECK-NOT: obelisk_sim.persistent_repetition_unbounded
// CHECK-COUNT-3: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_1.$concurrent_eos_count.31.repetition_strong
// CHECK: %[[NONCONSECUTIVE_0:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: %[[NONCONSECUTIVE_1:.*]] = obelisk_sim.ref.load
// CHECK-NEXT: %[[NONCONSECUTIVE_2:.*]] = obelisk_sim.ref.load
// CHECK: %[[NONCONSECUTIVE_ONE:.*]] = arith.constant {{.*}}1 : i64
// CHECK-NEXT: %[[NONCONSECUTIVE_ENTRY:.*]] = arith.addi %[[NONCONSECUTIVE_0]], %[[NONCONSECUTIVE_ONE]] : i64
// CHECK: %[[NONCONSECUTIVE_REPEAT:.*]] = obelisk_sim.assert.sampled_read
// CHECK-NEXT: %[[NONCONSECUTIVE_TRUE:.*]] = obelisk_sim.logic.is_true %[[NONCONSECUTIVE_REPEAT]]
// CHECK: %[[NONCONSECUTIVE_ZERO:.*]] = obelisk_sim.logic.constant false, false
// CHECK-NEXT: %[[NONCONSECUTIVE_FALSE:.*]] = obelisk_sim.logic.compare case_eq %[[NONCONSECUTIVE_REPEAT]], %[[NONCONSECUTIVE_ZERO]]
// CHECK-NEXT: %[[NONCONSECUTIVE_KNOWN:.*]] = arith.ori %[[NONCONSECUTIVE_TRUE]], %[[NONCONSECUTIVE_FALSE]]
// CHECK: %[[NONCONSECUTIVE_UNKNOWN:.*]] = arith.xori %[[NONCONSECUTIVE_KNOWN]],
// CHECK: %[[NONCONSECUTIVE_TERMINAL_SAMPLE:.*]] = obelisk_sim.assert.sampled_read
// CHECK-NEXT: %[[NONCONSECUTIVE_TERMINAL:.*]] = obelisk_sim.logic.is_true %[[NONCONSECUTIVE_TERMINAL_SAMPLE]]
// CHECK: %[[NONCONSECUTIVE_NOT_TERMINAL:.*]] = arith.xori %[[NONCONSECUTIVE_TERMINAL]],
// CHECK: %[[NONCONSECUTIVE_CONSUMES:.*]] = arith.andi %[[NONCONSECUTIVE_NOT_TERMINAL]], %[[NONCONSECUTIVE_TRUE]]
// CHECK-NEXT: %[[NONCONSECUTIVE_WAITS:.*]] = arith.andi %[[NONCONSECUTIVE_NOT_TERMINAL]], %[[NONCONSECUTIVE_FALSE]]
// CHECK-NEXT: %[[NONCONSECUTIVE_UNKNOWN_ACTIVE:.*]] = arith.andi %[[NONCONSECUTIVE_NOT_TERMINAL]], %[[NONCONSECUTIVE_UNKNOWN]]
// CHECK: arith.select %[[NONCONSECUTIVE_CONSUMES]], %[[NONCONSECUTIVE_2]],
// CHECK: arith.select %[[NONCONSECUTIVE_WAITS]], %[[NONCONSECUTIVE_2]],
// CHECK: arith.select %[[NONCONSECUTIVE_UNKNOWN_ACTIVE]], %[[NONCONSECUTIVE_2]],
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: obelisk_sim.spawn @unit_1.fork.31.0.2

// Exact [->0:0] constructs only pending count zero. Its entry count feeds that
// cell, the shared symbol is sampled once, and weak EOS completes survivors.
// CHECK-LABEL: obelisk_sim.func private @unit_2.$concurrent_eos_count.51.repetition_weak(
// CHECK-COUNT-1: obelisk_sim.ref.load
// CHECK-NOT: obelisk_sim.ref.load
// CHECK: obelisk_sim.spawn @unit_2.fork.51.0.0
// CHECK-LABEL: obelisk_sim.func private @unit_2(
// CHECK-SAME: obelisk_sim.persistent_repetition_kind = "goto"
// CHECK-SAME: obelisk_sim.persistent_repetition_max = 0 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_min = 0 : i64
// CHECK-SAME: obelisk_sim.persistent_repetition_states = 1 : i64
// CHECK-NOT: obelisk_sim.persistent_repetition_unbounded
// CHECK-COUNT-1: obelisk_sim.ref.alloc
// CHECK-NOT: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.spawn @unit_2.$concurrent_eos_count.51.repetition_weak
// CHECK: %[[ZERO_RANGE_PENDING:.*]] = obelisk_sim.ref.load
// CHECK: %[[ZERO_RANGE_ONE:.*]] = arith.constant {{.*}}1 : i64
// CHECK-NEXT: %[[ZERO_RANGE_ENTRY:.*]] = arith.addi %[[ZERO_RANGE_PENDING]], %[[ZERO_RANGE_ONE]] : i64
// CHECK: %[[ZERO_RANGE_SAMPLE:.*]] = obelisk_sim.assert.sampled_read
// CHECK-NEXT: %[[ZERO_RANGE_TRUE:.*]] = obelisk_sim.logic.is_true %[[ZERO_RANGE_SAMPLE]]
// CHECK-NOT: obelisk_sim.assert.sampled_read
// CHECK: arith.select %[[ZERO_RANGE_TRUE]], %[[ZERO_RANGE_ENTRY]],
// CHECK: arith.select %[[ZERO_RANGE_TRUE]], {{%.*}}, %[[ZERO_RANGE_ENTRY]]
// CHECK: obelisk_sim.spawn @unit_2.fork.51.0.0
// CHECK: obelisk_sim.spawn @unit_2.fork.51.1.2
