// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_nba_event_assignment", name = "unsupported_nba_event_assignment", node_id = 0 : i64, sym_name = "s0.unsupported_nba_event_assignment"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unsupported_nba_event_assignment", is_uninstantiated = false, name = "unsupported_nba_event_assignment", node_id = 3 : i64, referenced_path = "unsupported_nba_event_assignment", referenced_symbol = @s0.unsupported_nba_event_assignment, sym_name = "s3.unsupported_nba_event_assignment"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unsupported_nba_event_assignment", name = "unsupported_nba_event_assignment", node_id = 4 : i64, sym_name = "s4.unsupported_nba_event_assignment"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported_nba_event_assignment.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported_nba_event_assignment.lhs", lifetime = 1 : i32, name = "lhs", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.lhs"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported_nba_event_assignment.rhs", lifetime = 1 : i32, name = "rhs", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.rhs"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "unsupported_nba_event_assignment", node_id = 8 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, has_timing_control = true, node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 11 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "unsupported_nba_event_assignment.clk", referenced_symbol = @s1.$root::@s3.unsupported_nba_event_assignment::@s4.unsupported_nba_event_assignment::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
              obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "unsupported_nba_event_assignment.lhs", referenced_symbol = @s1.$root::@s3.unsupported_nba_event_assignment::@s4.unsupported_nba_event_assignment::@s6.lhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "unsupported_nba_event_assignment.rhs", referenced_symbol = @s1.$root::@s3.unsupported_nba_event_assignment::@s4.unsupported_nba_event_assignment::@s7.rhs, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: unsupported semantic node in the first simulation slice
// CHECK-SAME: scheduler-owned deferred action
