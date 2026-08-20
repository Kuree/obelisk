// RUN: %split-file %s %t
// RUN: obelisk-opt %t/kill.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=KILL
// RUN: obelisk-opt %t/kill.mlir '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %t/kill.mlir '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: not obelisk-opt %t/action.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=ACTION

// Kill invalidates live monitor state and queued reports through a per-target
// generation. Action controls support one-cycle concurrent evaluations; a
// multi-cycle selection must reject until its start-time snapshot is carried
// through temporal state.

//--- kill.mlir

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "assertion_control_concurrent_kill", name = "assertion_control_concurrent_kill", node_id = 0 : i64, sym_name = "s0.assertion_control_concurrent_kill"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "assertion_control_concurrent_kill", is_uninstantiated = false, name = "assertion_control_concurrent_kill", node_id = 3 : i64, referenced_path = "assertion_control_concurrent_kill", referenced_symbol = @s0.assertion_control_concurrent_kill, sym_name = "s3.assertion_control_concurrent_kill"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "assertion_control_concurrent_kill", name = "assertion_control_concurrent_kill", node_id = 4 : i64, sym_name = "s4.assertion_control_concurrent_kill", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "assertion_control_concurrent_kill.clk", name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "assertion_control_concurrent_kill.clk", lifetime = 1 : i32, name = "clk", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.clk"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "assertion_control_concurrent_kill.a", name = "a", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "assertion_control_concurrent_kill.a", lifetime = 1 : i32, name = "a", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.a"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "assertion_control_concurrent_kill", node_id = 9 : i64, procedure_kind = 0 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$assertkill", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 11 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.assertion_control_concurrent_kill", system_scope_path = "assertion_control_concurrent_kill", system_scope_symbol = @s1.$root::@s3.assertion_control_concurrent_kill::@s4.assertion_control_concurrent_kill} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "assertion_control_concurrent_kill.target", name = "target", node_id = 13 : i64, sym_name = "s10.target"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "assertion_control_concurrent_kill", node_id = 14 : i64, procedure_kind = 2 : i32, sym_name = "s11", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "assertion_control_concurrent_kill.target", block_symbol = @s1.$root::@s3.assertion_control_concurrent_kill::@s4.assertion_control_concurrent_kill::@s10.target, node_id = 15 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 16 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 17 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 18 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "assertion_control_concurrent_kill.clk", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent_kill::@s4.assertion_control_concurrent_kill::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 20 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 21 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "assertion_control_concurrent_kill.a", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent_kill::@s4.assertion_control_concurrent_kill::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 23 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "assertion_control_concurrent_kill.a", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent_kill::@s4.assertion_control_concurrent_kill::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 25 : i64} {
              }
            }
          }
        }
      }
    }
  }
}

//--- action.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "assertion_control_concurrent_action", name = "assertion_control_concurrent_action", node_id = 0 : i64, sym_name = "s0.assertion_control_concurrent_action"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "assertion_control_concurrent_action", is_uninstantiated = false, name = "assertion_control_concurrent_action", node_id = 3 : i64, referenced_path = "assertion_control_concurrent_action", referenced_symbol = @s0.assertion_control_concurrent_action, sym_name = "s3.assertion_control_concurrent_action"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "assertion_control_concurrent_action", name = "assertion_control_concurrent_action", node_id = 4 : i64, sym_name = "s4.assertion_control_concurrent_action", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "assertion_control_concurrent_action.clk", name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "assertion_control_concurrent_action.clk", lifetime = 1 : i32, name = "clk", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.clk"} {
        }
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "assertion_control_concurrent_action.a", name = "a", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "assertion_control_concurrent_action.a", lifetime = 1 : i32, name = "a", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.a"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "assertion_control_concurrent_action", node_id = 9 : i64, procedure_kind = 0 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$assertpassoff", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 11 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.assertion_control_concurrent_action", system_scope_path = "assertion_control_concurrent_action", system_scope_symbol = @s1.$root::@s3.assertion_control_concurrent_action::@s4.assertion_control_concurrent_action} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 13 : i64, referenced_path = "assertion_control_concurrent_action.target", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent_action::@s4.assertion_control_concurrent_action::@s10.target, semantic_type = !obelisk.void} {
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "assertion_control_concurrent_action.target", name = "target", node_id = 14 : i64, sym_name = "s10.target"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "assertion_control_concurrent_action", node_id = 15 : i64, procedure_kind = 2 : i32, sym_name = "s11", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "assertion_control_concurrent_action.target", block_symbol = @s1.$root::@s3.assertion_control_concurrent_action::@s4.assertion_control_concurrent_action::@s10.target, node_id = 16 : i64} {
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 17 : i64} {
              obelisk.sv.assertion.clocking attributes {node_id = 18 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 19 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 20 : i64, referenced_path = "assertion_control_concurrent_action.clk", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent_action::@s4.assertion_control_concurrent_action::@s6.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 21 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 22 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "assertion_control_concurrent_action.a", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent_action::@s4.assertion_control_concurrent_action::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 24 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "assertion_control_concurrent_action.a", referenced_symbol = @s1.$root::@s3.assertion_control_concurrent_action::@s4.assertion_control_concurrent_action::@s8.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 26 : i64} {
              }
            }
          }
        }
      }
    }
  }
}

// KILL: obelisk_sim.assert.control {{.*}} action 5 assertion
// KILL: obelisk_sim.concurrent_report_kill_epoch
// KILL: obelisk_sim.concurrent_kill_epoch_storage
// KILL: obelisk_sim.concurrent_kill_epoch_check
// ACTION: concurrent assertion action control currently requires a single-clock one-cycle directive without expect, abort, locals, persistent state, nonoverlapped handoff, or a vacuous branching-antecedent consequent
