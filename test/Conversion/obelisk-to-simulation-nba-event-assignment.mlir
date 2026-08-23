// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 2 : i32, hierarchical_name = "nba_event_assignment", name = "nba_event_assignment", node_id = 0 : i64, sym_name = "s0.nba_event_assignment"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "nba_event_assignment", is_uninstantiated = false, name = "nba_event_assignment", node_id = 3 : i64, referenced_path = "nba_event_assignment", referenced_symbol = @s0.nba_event_assignment, sym_name = "s3.nba_event_assignment"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "nba_event_assignment", name = "nba_event_assignment", node_id = 4 : i64, sym_name = "s4.nba_event_assignment"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "nba_event_assignment.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "nba_event_assignment.lhs", lifetime = 1 : i32, name = "lhs", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s6.lhs"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "nba_event_assignment.rhs", lifetime = 1 : i32, name = "rhs", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.rhs"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "nba_event_assignment.gate", lifetime = 1 : i32, name = "gate", node_id = 17 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s9.gate"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "nba_event_assignment.index", lifetime = 1 : i32, name = "index", node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s18.index"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "nba_event_assignment", node_id = 8 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, has_timing_control = true, node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 11 : i64} {
                obelisk.sv.expression.binary_op attributes {node_id = 12 : i64, operator_kind = 5 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "nba_event_assignment.clk", referenced_symbol = @s1.$root::@s3.nba_event_assignment::@s4.nba_event_assignment::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "nba_event_assignment.gate", referenced_symbol = @s1.$root::@s3.nba_event_assignment::@s4.nba_event_assignment::@s9.gate, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
              obelisk.sv.expression.element_select attributes {node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "nba_event_assignment.lhs", referenced_symbol = @s1.$root::@s3.nba_event_assignment::@s4.nba_event_assignment::@s6.lhs, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_path = "nba_event_assignment.index", referenced_symbol = @s1.$root::@s3.nba_event_assignment::@s4.nba_event_assignment::@s18.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "nba_event_assignment.rhs", referenced_symbol = @s1.$root::@s3.nba_event_assignment::@s4.nba_event_assignment::@s7.rhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
        }
      }
    }
  }
}

// The parent evaluates and captures both operands, launches a detached waiter,
// and returns without suspending. The child waits and only then stages the NBA.
// CHECK: obelisk_sim.code_unit.decl {{[0-9]+}} in 1 fork hierarchy "nba_event_assignment.$code_unit_8.$nba_event.10" debug "deferred NBA event" {internal}
// CHECK-LABEL: obelisk_sim.func private @{{.*nba_event.*}}(
// CHECK-SAME: domain = 1 : i32
// CHECK-SAME: home_region = 10 : i32
// CHECK-SAME: obelisk_sim.detached_controls
// CHECK-SAME: obelisk_sim.prime_on_spawn
// CHECK: %[[PRIMARY:.*]] = obelisk_sim.observer.bind
// CHECK-SAME: captures 2
// CHECK: obelisk_sim.suspend.observe %[[PRIMARY]]
// CHECK-SAME: edges [1]
// CHECK-SAME: ^[[COMMIT:[a-zA-Z0-9_]+]]
// CHECK: ^[[COMMIT]](
// CHECK: cf.br ^[[FINAL:[a-zA-Z0-9_]+]]
// CHECK: ^[[FINAL]]:
// CHECK: obelisk_sim.nba.enqueue %{{.*}} to %{{.*}}
// CHECK: obelisk_sim.return
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK: %[[RHS:.*]] = obelisk_sim.ref.load
// CHECK: %[[INDEX:.*]] = obelisk_sim.ref.load
// CHECK: %[[WIDE_INDEX:.*]] = arith.extsi %[[INDEX]]
// CHECK: %[[DESTINATION:.*]] = obelisk_sim.ref.array_element %{{.*}}[%[[WIDE_INDEX]]]
// CHECK-NEXT: %{{.*}} = obelisk_sim.spawn @{{.*nba_event.*}}(%{{.*}}, %[[DESTINATION]], %[[RHS]], %{{.*}})
// CHECK-NEXT: obelisk_sim.return
// CHECK-NOT: obelisk.sv.
