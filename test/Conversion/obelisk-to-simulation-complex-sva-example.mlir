// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// This MLIR-only example corresponds to:
//
//   assert property (@(posedge clk) disable iff (rst)
//     ((req ##[1:2] grant) or (req ##2 busy))
//     |=> ((busy or done) or error));
//
// The antecedent has three possible endpoints and the consequent has three.
// Every antecedent endpoint therefore needs three independent consequent
// traces. The asynchronous disable must cancel all nine live trace pairs.

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
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.req", lifetime = 1 : i32, name = "req", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.req"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.grant", lifetime = 1 : i32, name = "grant", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s8.grant"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.busy", lifetime = 1 : i32, name = "busy", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.busy"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.done", lifetime = 1 : i32, name = "done", node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s10.done"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.error", lifetime = 1 : i32, name = "error", node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s11.error"} {
        }

        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 12 : i64, procedure_kind = 2 : i32, sym_name = "s12", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 13 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 14 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 15 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.disable_iff attributes {node_id = 17 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 18 : i64, referenced_path = "top.rst", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.rst, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.binary attributes {node_id = 19 : i64, operator_kind = 12 : i32} {
                  obelisk.sv.assertion.binary attributes {node_id = 20 : i64, operator_kind = 1 : i32} {
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 1 : i64}], node_id = 21 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 22 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "top.req", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.req, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 24 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "top.grant", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.grant, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                    obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 2 : i64, min = 2 : i64}], node_id = 26 : i64} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 27 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 28 : i64, referenced_path = "top.req", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.req, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 29 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 30 : i64, referenced_path = "top.busy", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.busy, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                  }
                  obelisk.sv.assertion.binary attributes {node_id = 31 : i64, operator_kind = 1 : i32} {
                    obelisk.sv.assertion.binary attributes {node_id = 32 : i64, operator_kind = 1 : i32} {
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 33 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "top.busy", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.busy, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                      obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 35 : i64, repetition_is_unbounded = false} {
                        obelisk.sv.expression.named_value attributes {node_id = 36 : i64, referenced_path = "top.done", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s10.done, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                        }
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 37 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {node_id = 38 : i64, referenced_path = "top.error", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s11.error, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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
}

// The monitor runs in the Observed region and samples each referenced value
// once per clock tick. Ranged delays expand to three antecedent endpoints and
// three consequent alternatives, with one state machine per pair.
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: obelisk_sim.branching_antecedent_alternatives = 3 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_match_channels = 3 : i64
// CHECK-SAME: obelisk_sim.branching_antecedent_monitor
// CHECK-SAME: obelisk_sim.branching_consequent_alternatives = 3 : i64
// CHECK-SAME: obelisk_sim.combined_boolean_branching_monitor
// CHECK-SAME: obelisk_sim.combined_boolean_branching_pairs = 9 : i64
// CHECK: obelisk_sim.spawn @unit_0.$concurrent_cancel
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK-COUNT-5: obelisk_sim.assert.sampled_read
// CHECK: cf.cond_br
// CHECK: obelisk_sim.branching_antecedent_backedge
// CHECK-NOT: obelisk.sv.assertion
