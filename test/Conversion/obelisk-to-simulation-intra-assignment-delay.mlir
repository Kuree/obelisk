// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "simulation_intra_assignment_delay", name = "simulation_intra_assignment_delay", node_id = 0 : i64, sym_name = "s0.simulation_intra_assignment_delay"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "simulation_intra_assignment_delay", is_uninstantiated = false, name = "simulation_intra_assignment_delay", node_id = 3 : i64, referenced_path = "simulation_intra_assignment_delay", referenced_symbol = @s0.simulation_intra_assignment_delay, sym_name = "s3.simulation_intra_assignment_delay"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "simulation_intra_assignment_delay", name = "simulation_intra_assignment_delay", node_id = 4 : i64, sym_name = "s4.simulation_intra_assignment_delay"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_intra_assignment_delay.values", lifetime = 1 : i32, name = "values", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s5.values"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_intra_assignment_delay.rhs", lifetime = 1 : i32, name = "rhs", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.rhs"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_intra_assignment_delay.index", lifetime = 1 : i32, name = "index", node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s7.index"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_intra_assignment_delay.amount", lifetime = 1 : i32, name = "amount", node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s8.amount"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "simulation_intra_assignment_delay", node_id = 9 : i64, procedure_kind = 0 : i32, sym_name = "s9", time_precision_fs = 1000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 10 : i64} {
            obelisk.sv.statement.list attributes {node_id = 11 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 12 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, has_timing_control = true, node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.timing.delay attributes {node_id = 14 : i64} {
                    obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "simulation_intra_assignment_delay.amount", referenced_symbol = @s1.$root::@s3.simulation_intra_assignment_delay::@s4.simulation_intra_assignment_delay::@s8.amount, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.element_select attributes {node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "simulation_intra_assignment_delay.values", referenced_symbol = @s1.$root::@s3.simulation_intra_assignment_delay::@s4.simulation_intra_assignment_delay::@s5.values, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 18 : i64, referenced_path = "simulation_intra_assignment_delay.index", referenced_symbol = @s1.$root::@s3.simulation_intra_assignment_delay::@s4.simulation_intra_assignment_delay::@s7.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 19 : i64, referenced_path = "simulation_intra_assignment_delay.rhs", referenced_symbol = @s1.$root::@s3.simulation_intra_assignment_delay::@s4.simulation_intra_assignment_delay::@s6.rhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 20 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, has_timing_control = true, node_id = 21 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.timing.delay attributes {node_id = 22 : i64} {
                    obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "simulation_intra_assignment_delay.amount", referenced_symbol = @s1.$root::@s3.simulation_intra_assignment_delay::@s4.simulation_intra_assignment_delay::@s8.amount, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.element_select attributes {node_id = 24 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "simulation_intra_assignment_delay.values", referenced_symbol = @s1.$root::@s3.simulation_intra_assignment_delay::@s4.simulation_intra_assignment_delay::@s5.values, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 26 : i64, referenced_path = "simulation_intra_assignment_delay.index", referenced_symbol = @s1.$root::@s3.simulation_intra_assignment_delay::@s4.simulation_intra_assignment_delay::@s7.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 27 : i64, referenced_path = "simulation_intra_assignment_delay.rhs", referenced_symbol = @s1.$root::@s3.simulation_intra_assignment_delay::@s4.simulation_intra_assignment_delay::@s6.rhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

// Blocking intra-assignment timing evaluates the RHS before suspension and
// resolves the dynamic destination in the continuation.
// CHECK: %[[BLOCKING_RHS:.*]] = obelisk_sim.ref.load
// CHECK: %[[BLOCKING_DELAY:.*]] = obelisk_sim.time.scale
// CHECK: obelisk_sim.suspend.delay %[[BLOCKING_DELAY]] to ^[[COMMIT:bb[0-9]+]](%[[BLOCKING_RHS]] : !obelisk_sim.logic<1>)
// CHECK: ^[[COMMIT]](%[[COMMIT_RHS:.*]]: !obelisk_sim.logic<1>)
// CHECK: obelisk_sim.ref.load
// CHECK: obelisk_sim.ref.array_element
// CHECK: obelisk_sim.ref.store %[[COMMIT_RHS]]

// A timed nonblocking assignment captures its destination and value at
// encounter time and queues the update without suspending the caller.
// CHECK: %[[NBA_DELAY:.*]] = obelisk_sim.time.scale
// CHECK: %[[NBA_DEST:.*]] = obelisk_sim.ref.array_element
// CHECK: obelisk_sim.nba.enqueue {{%.*}} to %[[NBA_DEST]] after %[[NBA_DELAY]]
// CHECK-NOT: obelisk_sim.suspend.delay
// CHECK-NOT: obelisk.sv.
