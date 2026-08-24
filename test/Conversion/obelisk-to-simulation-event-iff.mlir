// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_event_iff", name = "unsupported_event_iff", node_id = 0 : i64, sym_name = "s0.unsupported_event_iff"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unsupported_event_iff", is_uninstantiated = false, name = "unsupported_event_iff", node_id = 3 : i64, referenced_path = "unsupported_event_iff", referenced_symbol = @s0.unsupported_event_iff, sym_name = "s3.unsupported_event_iff"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unsupported_event_iff", name = "unsupported_event_iff", node_id = 4 : i64, sym_name = "s4.unsupported_event_iff"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported_event_iff.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported_event_iff.enable", lifetime = 1 : i32, name = "enable", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.enable"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported_event_iff.value", lifetime = 1 : i32, name = "value", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.value"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "unsupported_event_iff", node_id = 8 : i64, procedure_kind = 2 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 9 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = true, node_id = 10 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "unsupported_event_iff.clk", referenced_symbol = @s1.$root::@s3.unsupported_event_iff::@s4.unsupported_event_iff::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "unsupported_event_iff.enable", referenced_symbol = @s1.$root::@s3.unsupported_event_iff::@s4.unsupported_event_iff::@s6.enable, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "unsupported_event_iff.value", referenced_symbol = @s1.$root::@s3.unsupported_event_iff::@s4.unsupported_event_iff::@s7.value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "unsupported_event_iff.enable", referenced_symbol = @s1.$root::@s3.unsupported_event_iff::@s4.unsupported_event_iff::@s6.enable, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: obelisk_sim.suspend.edge_iff posedge
// CHECK: obelisk_sim.ref.store
// CHECK-NOT: cf.cond_br
// CHECK-NOT: obelisk.sv.
