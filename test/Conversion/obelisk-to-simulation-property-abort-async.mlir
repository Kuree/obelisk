// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' -o %t.threaded
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' --mlir-disable-threading -o %t.serial
// RUN: diff %t.threaded %t.serial

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64} {
        obelisk.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s6.a attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s7.b attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s8.reset attributes {hierarchical_name = "top.reset", lifetime = 1 : i32, name = "reset", node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s9.hit attributes {hierarchical_name = "top.hit", lifetime = 1 : i32, name = "hit", node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }

        // cover property accept_on(reset) (a ##1 b)
        obelisk.sv.symbol.procedural_block @s10 attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 2 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 11 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 12 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.abort attributes {action = 0 : i32, is_synchronous = false, node_id = 15 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "top.reset", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 17 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 18 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 20 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 22 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 23 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 24 : i64, referenced_path = "top.hit", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.hit, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
          }
        }

        // reject_on(reset) (a ##1 b)
        obelisk.sv.symbol.procedural_block @s30 attributes {hierarchical_name = "top", node_id = 30 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 31 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 32 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 33 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.assertion.abort attributes {action = 1 : i32, is_synchronous = false, node_id = 35 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 36 : i64, referenced_path = "top.reset", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.reset, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.assertion.sequence_concat attributes {delays = [{is_unbounded = false, max = 0 : i64, min = 0 : i64}, {is_unbounded = false, max = 1 : i64, min = 1 : i64}], node_id = 37 : i64} {
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 38 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 39 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 40 : i64, repetition_is_unbounded = false} {
                    obelisk.sv.expression.named_value attributes {node_id = 41 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

// Accepted asynchronous aborts are vacuous successful cover-property
// evaluations. The detached Reactive observer reevaluates the Preponed-sampled
// condition once per time slot, invokes the pass action once per live attempt,
// and clears state.
// CHECK: simulation.func private @[[ASYNC_PASS:unit_0\.fork\.11\.0\.0]](
// CHECK: simulation.ref.load
// CHECK: simulation.ref.store
// CHECK-LABEL: simulation.func private @unit_0.$concurrent_abort.11(
// CHECK-SAME: domain = 0 : i32
// CHECK-SAME: home_region = 10 : i32
// CHECK-SAME: schedule.concurrent_abort
// CHECK-SAME: schedule.detached_controls
// CHECK: simulation.observer.bind
// CHECK-SAME: values(%arg1, %arg2 : !simulation.ref<!simulation.logic<1>>, !simulation.event) captures 1
// CHECK: simulation.suspend.observe
// CHECK-SAME: resume_region = 10 : i32
// CHECK-SAME: schedule.concurrent_abort_level_true
// CHECK: arith.andi
// CHECK: cf.cond_br
// CHECK: simulation.spawn @[[ASYNC_PASS]]
// CHECK-NOT: simulation.spawn @[[ASYNC_PASS]]
// CHECK: simulation.ref.store
// CHECK: cf.br

// The clocked monitor binds the observer to the private Preponed-snapshot event
// and tests the sampled abort before any sampled a/b predicate read.
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: simulation.asynchronous_property_abort
// CHECK-SAME: simulation.property_abort_action = "accept"
// CHECK: [[PREPONED:%.*]] = simulation.context.event %arg0[2305843009213693952]
// CHECK: simulation.spawn @unit_0.$concurrent_abort.11
// CHECK-SAME: [[PREPONED]]
// CHECK: simulation.suspend.edge
// CHECK: simulation.ref.load {{%.*}} : !simulation.ref<i64>
// CHECK-NEXT: [[CLOCK_RESET:%.*]] = simulation.assert.sampled_read %arg0 from %arg4
// CHECK: [[CLOCK_RESET_TRUE:%.*]] = simulation.logic.is_true [[CLOCK_RESET]]
// CHECK: cf.cond_br [[CLOCK_RESET_TRUE]]
// CHECK: simulation.assert.sampled_read

// Rejection dispatches the ordinary failure callback once for each live bit,
// then tears all attempts down. This callback is absent from accept_on above.
// CHECK-LABEL: simulation.func private @unit_1.$concurrent_abort.31(
// CHECK-SAME: home_region = 10 : i32
// CHECK-SAME: schedule.concurrent_abort
// CHECK: simulation.observer.bind
// CHECK-SAME: values(%arg1, %arg2 : !simulation.ref<!simulation.logic<1>>, !simulation.event) captures 1
// CHECK: simulation.suspend.observe
// CHECK: arith.andi
// CHECK: cf.cond_br
// CHECK: simulation.spawn @unit_1.fork.31.0.2
// CHECK: simulation.ref.store
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: simulation.asynchronous_property_abort
// CHECK-SAME: simulation.property_abort_action = "reject"
// CHECK: [[PREPONED1:%.*]] = simulation.context.event %arg0[2305843009213693952]
// CHECK: simulation.spawn @unit_1.$concurrent_abort.31
// CHECK-SAME: [[PREPONED1]]
// CHECK: simulation.suspend.edge
// CHECK: simulation.assert.sampled_read %arg0 from %arg4

// Both observer evaluators read the global Preponed snapshot, never a raw
// current value that may have changed later in the time slot.
// CHECK-LABEL: simulation.func private @observer_
// CHECK-SAME: schedule.concurrent_abort_observer
// CHECK: simulation.assert.sampled_read %arg0 from %arg1
// CHECK-LABEL: simulation.func private @observer_
// CHECK-SAME: schedule.concurrent_abort_observer
// CHECK: simulation.assert.sampled_read %arg0 from %arg1
