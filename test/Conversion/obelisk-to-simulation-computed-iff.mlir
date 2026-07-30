// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "simulation_computed_iff", name = "simulation_computed_iff", node_id = 0 : i64, sym_name = "s0.simulation_computed_iff"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "simulation_computed_iff", is_uninstantiated = false, name = "simulation_computed_iff", node_id = 3 : i64, referenced_path = "simulation_computed_iff", referenced_symbol = @s0.simulation_computed_iff, sym_name = "s3.simulation_computed_iff"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "simulation_computed_iff", name = "simulation_computed_iff", node_id = 4 : i64, sym_name = "s4.simulation_computed_iff"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_computed_iff.clock", lifetime = 1 : i32, name = "clock", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clock"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_computed_iff.left", lifetime = 1 : i32, name = "left", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.left"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_computed_iff.right", lifetime = 1 : i32, name = "right", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.right"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "simulation_computed_iff", node_id = 8 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 9 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = true, node_id = 10 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "simulation_computed_iff.clock", referenced_symbol = @s1.$root::@s3.simulation_computed_iff::@s4.simulation_computed_iff::@s5.clock, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.binary_op attributes {node_id = 12 : i64, operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "simulation_computed_iff.left", referenced_symbol = @s1.$root::@s3.simulation_computed_iff::@s4.simulation_computed_iff::@s6.left, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "simulation_computed_iff.right", referenced_symbol = @s1.$root::@s3.simulation_computed_iff::@s4.simulation_computed_iff::@s7.right, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "simulation_computed_iff.left", referenced_symbol = @s1.$root::@s3.simulation_computed_iff::@s4.simulation_computed_iff::@s6.left, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.conversion attributes {node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.conversion attributes {node_id = 19 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// CHECK: obelisk_sim.code_unit.decl {{[0-9]+}} in 1 observer
// CHECK: obelisk_sim.code_unit.decl {{[0-9]+}} in 1 observer
// CHECK: %[[PRIMARY:.*]] = obelisk_sim.observer.bind
// CHECK-SAME: : <!obelisk_sim.logic<1>>
// CHECK: %[[CONDITION:.*]] = obelisk_sim.observer.bind
// CHECK-SAME: : <i1>
// CHECK: obelisk_sim.suspend.observe %[[PRIMARY]], {{%.*}}, %[[CONDITION]]
// CHECK-SAME: conditions 1 edges [1] indices [0]
// CHECK-NOT: obelisk.sv.
