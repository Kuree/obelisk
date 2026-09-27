// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 9.4.2 spells an event expression as an optional edge
// identifier, an expression, and an optional `iff` qualifier, so a level
// change qualified by `iff` is as legal as an edge qualified by one: A.6.5's
// event_expression makes the edge_identifier optional independently of the
// iff. The waiting process resumes on a change of the primary signal whose
// condition is true at that moment.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "event_control_iff", name = "event_control_iff", node_id = 0 : i64, sym_name = "s0.event_control_iff"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "event_control_iff", is_uninstantiated = false, name = "event_control_iff", node_id = 3 : i64, referenced_path = "event_control_iff", referenced_symbol = @s0.event_control_iff, sym_name = "s3.event_control_iff"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "event_control_iff", name = "event_control_iff", node_id = 4 : i64, sym_name = "s4.event_control_iff", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "event_control_iff.d", lifetime = 1 : i32, name = "d", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s5.d"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "event_control_iff.enable", lifetime = 1 : i32, name = "enable", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.enable"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "event_control_iff.captured", lifetime = 1 : i32, name = "captured", node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s7.captured"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "event_control_iff", node_id = 8 : i64, procedure_kind = 2 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 9 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = true, node_id = 10 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "event_control_iff.d", referenced_symbol = @s1.$root::@s3.event_control_iff::@s4.event_control_iff::@s5.d, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "event_control_iff.enable", referenced_symbol = @s1.$root::@s3.event_control_iff::@s4.event_control_iff::@s6.enable, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, is_signed = false, node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "event_control_iff.captured", referenced_symbol = @s1.$root::@s3.event_control_iff::@s4.event_control_iff::@s7.captured, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "event_control_iff.d", referenced_symbol = @s1.$root::@s3.event_control_iff::@s4.event_control_iff::@s5.d, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}


// CHECK-LABEL: simulation.func private @unit_0
// CHECK: simulation.suspend.edge_iff change %{{.*}} iff %{{.*}} to
