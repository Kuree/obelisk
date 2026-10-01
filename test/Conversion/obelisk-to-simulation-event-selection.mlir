// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition @s0.simulation_event_selection attributes {definition_kind = 0 : i32, hierarchical_name = "simulation_event_selection", name = "simulation_event_selection", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.simulation_event_selection attributes {hierarchical_name = "simulation_event_selection", is_uninstantiated = false, name = "simulation_event_selection", node_id = 3 : i64, referenced_path = "simulation_event_selection", referenced_symbol = @s0.simulation_event_selection} {
      obelisk.sv.symbol.instance_body @s4.simulation_event_selection attributes {hierarchical_name = "simulation_event_selection", name = "simulation_event_selection", node_id = 4 : i64} {
        obelisk.sv.symbol.variable @s5.values attributes {hierarchical_name = "simulation_event_selection.values", lifetime = 1 : i32, name = "values", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
        }
        obelisk.sv.symbol.variable @s6.index attributes {hierarchical_name = "simulation_event_selection.index", lifetime = 1 : i32, name = "index", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.variable @s8.events attributes {hierarchical_name = "simulation_event_selection.events", lifetime = 1 : i32, name = "events", node_id = 17 : i64, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.event, false>} {
        }
        obelisk.sv.symbol.variable @s9.event_index attributes {hierarchical_name = "simulation_event_selection.event_index", lifetime = 1 : i32, name = "event_index", node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.procedural_block @s7 attributes {hierarchical_name = "simulation_event_selection", node_id = 7 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 8 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 9 : i64} {
              obelisk.sv.expression.element_select attributes {node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "simulation_event_selection.values", referenced_symbol = @s1.$root::@s3.simulation_event_selection::@s4.simulation_event_selection::@s5.values, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "simulation_event_selection.index", referenced_symbol = @s1.$root::@s3.simulation_event_selection::@s4.simulation_event_selection::@s6.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "simulation_event_selection.values", referenced_symbol = @s1.$root::@s3.simulation_event_selection::@s4.simulation_event_selection::@s5.values, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
                obelisk.sv.expression.unbased_unsized_integer_literal attributes {constant_value = "8'd0", node_id = 16 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s10 attributes {hierarchical_name = "simulation_event_selection", node_id = 19 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 20 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 21 : i64} {
              obelisk.sv.expression.element_select attributes {node_id = 22 : i64, semantic_type = !obelisk.event} {
                obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "simulation_event_selection.events", referenced_symbol = @s1.$root::@s3.simulation_event_selection::@s4.simulation_event_selection::@s8.events, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.event, false>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 24 : i64, referenced_path = "simulation_event_selection.event_index", referenced_symbol = @s1.$root::@s3.simulation_event_selection::@s4.simulation_event_selection::@s9.event_index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// CHECK: simulation.observer.bind
// CHECK-SAME: captures 2 : !simulation.observer<!simulation.logic<1>>
// CHECK: simulation.suspend.observe
// CHECK-SAME: conditions 0 edges [0] indices [-1]
// CHECK: %[[SELECTED_EVENT:.*]] = simulation.assoc.read
// CHECK: %[[EVENT_OBSERVER:.*]] = simulation.observer.bind
// CHECK-SAME: %[[SELECTED_EVENT]]
// CHECK-SAME: !simulation.event
// CHECK: simulation.suspend.observe %[[EVENT_OBSERVER]],
// CHECK: simulation.func private @observer_
// CHECK: %[[ELEMENT:.*]] = simulation.ref.array_element
// CHECK: simulation.ref.load %[[ELEMENT]]
// CHECK-NOT: obelisk.sv.
