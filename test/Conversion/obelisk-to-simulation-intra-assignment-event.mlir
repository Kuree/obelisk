// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "simulation_intra_assignment_event", name = "simulation_intra_assignment_event", node_id = 0 : i64, sym_name = "s0.simulation_intra_assignment_event"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "simulation_intra_assignment_event", is_uninstantiated = false, name = "simulation_intra_assignment_event", node_id = 3 : i64, referenced_path = "simulation_intra_assignment_event", referenced_symbol = @s0.simulation_intra_assignment_event, sym_name = "s3.simulation_intra_assignment_event"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "simulation_intra_assignment_event", name = "simulation_intra_assignment_event", node_id = 4 : i64, sym_name = "s4.simulation_intra_assignment_event"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_intra_assignment_event.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_intra_assignment_event.lhs", lifetime = 1 : i32, name = "lhs", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.lhs"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_intra_assignment_event.rhs", lifetime = 1 : i32, name = "rhs", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.rhs"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "simulation_intra_assignment_event", node_id = 8 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 9 : i64} {
            obelisk.sv.statement.list attributes {node_id = 10 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, has_timing_control = true, node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 13 : i64} {
                    obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "simulation_intra_assignment_event.clk", referenced_symbol = @s1.$root::@s3.simulation_intra_assignment_event::@s4.simulation_intra_assignment_event::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "simulation_intra_assignment_event.lhs", referenced_symbol = @s1.$root::@s3.simulation_intra_assignment_event::@s4.simulation_intra_assignment_event::@s6.lhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "simulation_intra_assignment_event.rhs", referenced_symbol = @s1.$root::@s3.simulation_intra_assignment_event::@s4.simulation_intra_assignment_event::@s7.rhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 17 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, has_timing_control = true, node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.timing.repeated_event attributes {node_id = 19 : i64} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 21 : i64} {
                      obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "simulation_intra_assignment_event.clk", referenced_symbol = @s1.$root::@s3.simulation_intra_assignment_event::@s4.simulation_intra_assignment_event::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                      }
                    }
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "simulation_intra_assignment_event.lhs", referenced_symbol = @s1.$root::@s3.simulation_intra_assignment_event::@s4.simulation_intra_assignment_event::@s6.lhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 24 : i64, referenced_path = "simulation_intra_assignment_event.rhs", referenced_symbol = @s1.$root::@s3.simulation_intra_assignment_event::@s4.simulation_intra_assignment_event::@s7.rhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

// The RHS is loaded before each wait, while the blocking destination store is
// emitted only in its continuation.
// CHECK: %[[EVENT_RHS:.*]] = obelisk_sim.ref.load
// CHECK: obelisk_sim.suspend.edge posedge {{.*}} to ^[[EVENT_COMMIT:.*]](%[[EVENT_RHS]]
// CHECK: ^[[EVENT_COMMIT]](%[[EVENT_COMMIT_RHS:.*]]: !obelisk_sim.logic<1>):
// CHECK: obelisk_sim.ref.store %[[EVENT_COMMIT_RHS]]
// CHECK: %[[REPEAT_RHS:.*]] = obelisk_sim.ref.load
// CHECK: cf.br ^[[REPEAT_HEADER:.*]](%{{.*}}, %[[REPEAT_RHS]]
// CHECK: ^[[REPEAT_COMMIT:.*]](%[[REPEAT_COMMIT_RHS:.*]]: !obelisk_sim.logic<1>):
// CHECK: obelisk_sim.ref.store %[[REPEAT_COMMIT_RHS]]
// CHECK: ^[[REPEAT_HEADER]](%[[REPEAT_COUNT:.*]]: i64, %[[REPEAT_VALUE:.*]]: !obelisk_sim.logic<1>):
// CHECK: obelisk_sim.suspend.edge posedge {{.*}} to ^[[REPEAT_STEP:.*]](%[[REPEAT_COUNT]], %[[REPEAT_VALUE]]
// CHECK: ^[[REPEAT_STEP]](%[[STEP_COUNT:.*]]: i64, %[[STEP_VALUE:.*]]: !obelisk_sim.logic<1>):
// CHECK: %[[NEXT_COUNT:.*]] = arith.subi %[[STEP_COUNT]]
// CHECK: %[[CONTINUE:.*]] = arith.cmpi sgt, %[[NEXT_COUNT]]
// CHECK: cf.cond_br %[[CONTINUE]], ^[[REPEAT_HEADER]](%[[NEXT_COUNT]], %[[STEP_VALUE]] {{.*}}), ^[[REPEAT_COMMIT]](%[[STEP_VALUE]]
// CHECK-NOT: obelisk.sv.
