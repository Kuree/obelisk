// RUN: %split-file %s %t
// RUN: not obelisk-opt %t/multicycle-antecedent.mlir '--lower-obelisk-to-sim=opt-level=0' -o /dev/null 2>&1 | FileCheck %s --check-prefix=MULTICYCLE
// RUN: obelisk-opt %t/explicit-prefix.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=PREFIX
// RUN: obelisk-opt %t/explicit-prefix.mlir '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %t/explicit-prefix.mlir '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// MULTICYCLE: error: unbounded implication/followed-by delay currently requires one Boolean antecedent and a deterministic bounded consequent prefix before the final ##[M:$] Boolean terminal

// A successful antecedent is handed off for one clock and then activates,
// rather than bypasses, the two-cycle deterministic prefix. Prefix failures
// join the monitor's counted failure path. The observable pass action keeps
// the prefix, warm-up, eligible, and handoff state live through finite end.
// PREFIX-LABEL: obelisk_sim.func private @unit_0.$concurrent_eos_count.109.delay_weak(
// PREFIX-COUNT-4: obelisk_sim.ref.load
// PREFIX-LABEL: obelisk_sim.func private @unit_0(
// PREFIX-SAME: obelisk_sim.persistent_delay_implication
// PREFIX-SAME: obelisk_sim.persistent_delay_minimum = 1 : i64
// PREFIX-SAME: obelisk_sim.persistent_delay_nonoverlapped
// PREFIX-SAME: obelisk_sim.persistent_delay_prefix_horizon = 2 : i64
// The antecedent/prefix share a, and the prefix/terminal share b. Each static
// symbol is sampled once for the complete Observed transition.
// PREFIX: obelisk_sim.assert.sampled_read %{{.*}} from %arg2
// PREFIX: obelisk_sim.ref.load
// PREFIX: arith.cmpi ne
// PREFIX: obelisk_sim.ref.store
// PREFIX: arith.andi
// PREFIX: arith.cmpi ne
// PREFIX: arith.andi
// PREFIX: arith.extui
// PREFIX: obelisk_sim.assert.sampled_read %{{.*}} from %arg3
// Terminal success is dispatched before older/current prefix failures, which
// in turn precede the current false-antecedent vacuous success.
// PREFIX-NOT: obelisk_sim.assert.sampled_read
// PREFIX: obelisk_sim.spawn @unit_0.fork.109.0.0
// PREFIX: obelisk_sim.spawn @unit_0.fork.109.1.2
// PREFIX: obelisk_sim.spawn @unit_0.fork.109.0.0

//--- multicycle-antecedent.mlir

module {
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
              obelisk.sv.assertion.binary attributes {node_id = 13 : i64, operator_kind = 11 : i32} {
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 14 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 15 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 17 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 18 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = true, min = 1 : i64}], node_id = 19 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 20 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

//--- explicit-prefix.mlir

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 100 : i64, sym_name = "p0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 101 : i64, sym_name = "p1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 102 : i64, sym_name = "p2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 103 : i64, referenced_path = "top", referenced_symbol = @p0.top, sym_name = "p3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 104 : i64, sym_name = "p4.top"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 105 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "p5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 106 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "p6.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 107 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "p7.b"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 108 : i64, procedure_kind = 2 : i32, sym_name = "p8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 109 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 110 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 111 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 112 : i64, referenced_path = "top.clk", referenced_symbol = @p1.$root::@p3.top::@p4.top::@p5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.binary attributes {node_id = 113 : i64, operator_kind = 12 : i32} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 114 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {node_id = 115 : i64, referenced_path = "top.a", referenced_symbol = @p1.$root::@p3.top::@p4.top::@p6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}, {is_unbounded = true, min = 1 : i64}], node_id = 116 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 117 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 118 : i64, referenced_path = "top.b", referenced_symbol = @p1.$root::@p3.top::@p4.top::@p7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 119 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 120 : i64, referenced_path = "top.a", referenced_symbol = @p1.$root::@p3.top::@p4.top::@p6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 121 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 122 : i64, referenced_path = "top.b", referenced_symbol = @p1.$root::@p3.top::@p4.top::@p7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 123 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 124 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 125 : i64, referenced_path = "top.b", referenced_symbol = @p1.$root::@p3.top::@p4.top::@p7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 126 : i64, referenced_path = "top.a", referenced_symbol = @p1.$root::@p3.top::@p4.top::@p6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
          }
        }
      }
    }
  }
}
