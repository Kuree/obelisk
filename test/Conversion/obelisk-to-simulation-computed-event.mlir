// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_computed_event", name = "unsupported_computed_event", node_id = 0 : i64, sym_name = "s0.unsupported_computed_event"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unsupported_computed_event", is_uninstantiated = false, name = "unsupported_computed_event", node_id = 3 : i64, referenced_path = "unsupported_computed_event", referenced_symbol = @s0.unsupported_computed_event, sym_name = "s3.unsupported_computed_event"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unsupported_computed_event", name = "unsupported_computed_event", node_id = 4 : i64, sym_name = "s4.unsupported_computed_event"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported_computed_event.lhs", lifetime = 1 : i32, name = "lhs", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.lhs"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported_computed_event.rhs", lifetime = 1 : i32, name = "rhs", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.rhs"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported_computed_event.result", lifetime = 1 : i32, name = "result", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "unsupported_computed_event", node_id = 8 : i64, procedure_kind = 2 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 9 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 10 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 11 : i64, operator_kind = 5 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "unsupported_computed_event.lhs", referenced_symbol = @s1.$root::@s3.unsupported_computed_event::@s4.unsupported_computed_event::@s5.lhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "unsupported_computed_event.rhs", referenced_symbol = @s1.$root::@s3.unsupported_computed_event::@s4.unsupported_computed_event::@s6.rhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, node_id = 15 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "unsupported_computed_event.result", referenced_symbol = @s1.$root::@s3.unsupported_computed_event::@s4.unsupported_computed_event::@s7.result, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.conversion attributes {node_id = 17 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 18 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
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

// CHECK: obelisk_sim.observer.bind
// CHECK: obelisk_sim.suspend.observe
// CHECK-SAME: conditions 0 edges [1] indices [-1]
// CHECK: obelisk_sim.func private @observer_
// CHECK: obelisk_sim.logic.binary and
// CHECK-NOT: obelisk.sv.
