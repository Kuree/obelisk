// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 16.14.6: a matured procedural assertion instance evaluates
// as a concurrent attempt started on its leading clock, one sampled tick per
// sequence age, and is not affected by any later disable (16.14.6.4). It
// begins at once when that clock occurred in the current time step, and
// otherwise at the next occurrence. A resumption from an event control
// flushes pending instances (16.14.6.2); disabling a label flushes only that
// assertion's pending instances, and disabling the outermost scope flushes
// all of them (16.14.6.4).
//
// module top;
//   bit clk, a, b;
//   always @(posedge clk) assert property (a ##1 b);
//   initial begin
//     @(negedge clk);
//     c: assert property (@(posedge clk) a);
//     disable c;
//   end
//   initial begin : o
//     assert property (@(posedge clk) b);
//     disable o;
//   end
// endmodule

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.variable @s6.a attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.variable @s7.b attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.procedural_block @s8 attributes {hierarchical_name = "top", node_id = 8 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 9 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 10 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
            obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_contextual_clock = true, has_default_disable = false, has_fail_action = false, has_pass_action = true, is_procedural = true, node_id = 12 : i64, starts_on_current_clock = true} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                }
              }
              obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 15 : i64} {
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 16 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                }
                obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 18 : i64, repetition_is_unbounded = false} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 20 : i64} {
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block @s9.c attributes {block_kind = 0 : i32, hierarchical_name = "top.c", name = "c", node_id = 21 : i64} {
        }
        obelisk.sv.symbol.procedural_block @s10 attributes {hierarchical_name = "top", node_id = 22 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 23 : i64} {
            obelisk.sv.statement.list attributes {node_id = 24 : i64} {
              obelisk.sv.statement.timed attributes {node_id = 25 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 2 : i32, has_iff = false, node_id = 26 : i64} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 27 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 28 : i64} {
                }
              }
              obelisk.sv.statement.block attributes {block_path = "top.c", block_symbol = @s1.$root::@s3.top::@s4.top::@s9.c, node_id = 29 : i64} {
                obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, is_procedural = true, node_id = 30 : i64} {
                  obelisk.sv.assertion.clocking attributes {node_id = 31 : i64} {
                    obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 32 : i64} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 33 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      }
                    }
                    obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 34 : i64, repetition_is_unbounded = false} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 35 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      }
                    }
                  }
                  obelisk.sv.statement.empty attributes {node_id = 36 : i64} {
                  }
                }
              }
              obelisk.sv.statement.disable attributes {node_id = 37 : i64, target_path = "top.c", target_symbol = @s1.$root::@s3.top::@s4.top::@s9.c} {
              }
            }
          }
        }
        obelisk.sv.symbol.statement_block @s11.o attributes {block_kind = 0 : i32, hierarchical_name = "top.o", name = "o", node_id = 38 : i64} {
        }
        obelisk.sv.symbol.procedural_block @s12 attributes {hierarchical_name = "top", node_id = 39 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {block_path = "top.o", block_symbol = @s1.$root::@s3.top::@s4.top::@s11.o, node_id = 40 : i64} {
            obelisk.sv.statement.list attributes {node_id = 41 : i64} {
              obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, is_procedural = true, node_id = 42 : i64, outer_process_scope_named = true} {
                obelisk.sv.assertion.clocking attributes {node_id = 43 : i64} {
                  obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 44 : i64} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 45 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 46 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 47 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    }
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 48 : i64} {
                }
              }
              obelisk.sv.statement.disable attributes {node_id = 49 : i64, target_path = "top.o", target_symbol = @s1.$root::@s3.top::@s4.top::@s11.o} {
              }
            }
          }
        }
      }
    }
  }
}


// The always attempt walks `a` on its own tick, then waits one clock in
// Observed for `b`.
// CHECK-LABEL: simulation.func private @unit_0.fork.12.0.48(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: schedule.detached_controls
// CHECK: simulation.assert.sampled_read
// CHECK: simulation.suspend.edge posedge {{.*}}resume_region = 8 : i32
// CHECK: simulation.assert.sampled_read

// Each resumption of the always procedure advances its queue epoch before
// the new instance is queued.
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: simulation.suspend.edge posedge
// CHECK: ^bb2([[QUEUE:%.+]]: !simulation.ref<i64>):
// CHECK: arith.addi
// CHECK-NEXT: simulation.ref.store {{.*}} to [[QUEUE]]
// CHECK: simulation.spawn @unit_0.fork.12.0.48

// The negedge-queued instance tests whether its posedge already occurred in
// this time step before waiting for one, and is also current only while its
// label was not disabled.
// CHECK-LABEL: simulation.func private @unit_1.fork.30.0.48(
// CHECK: arith.cmpi eq
// CHECK: arith.cmpi eq
// CHECK: arith.andi
// CHECK: simulation.clocking_output.current posedge
// CHECK: simulation.suspend.edge posedge {{.*}}resume_region = 8 : i32
// CHECK: simulation.assert.sampled_read

// CHECK-LABEL: simulation.func private @unit_1(
// CHECK: simulation.ref.alloc
// CHECK: simulation.suspend.edge negedge
// CHECK: simulation.spawn @unit_1.fork.30.0.48
// CHECK: simulation.ref.load [[LABEL_ARG:%.+]] :
// CHECK: arith.addi
// CHECK-NEXT: simulation.ref.store {{.*}} to [[LABEL_ARG]]
// CHECK-NEXT: simulation.return

// CHECK-LABEL: simulation.func private @unit_2(
// CHECK: [[OUTER_QUEUE:%.+]] = simulation.ref.alloc
// CHECK: simulation.spawn @unit_2.fork.42.0.48
// CHECK: simulation.control.disable
// CHECK: simulation.ref.load [[OUTER_QUEUE]]
// CHECK: arith.addi
// CHECK-NEXT: simulation.ref.store {{.*}} to [[OUTER_QUEUE]]
// CHECK-NEXT: cf.br
