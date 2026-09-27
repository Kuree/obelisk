// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "untimed_always_startup", name = "untimed_always_startup", node_id = 0 : i64, sym_name = "s0.untimed_always_startup"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "untimed_always_startup", is_uninstantiated = false, name = "untimed_always_startup", node_id = 3 : i64, referenced_path = "untimed_always_startup", referenced_symbol = @s0.untimed_always_startup, sym_name = "s3.untimed_always_startup"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "untimed_always_startup", name = "untimed_always_startup", node_id = 4 : i64, sym_name = "s4.untimed_always_startup", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "untimed_always_startup.source", lifetime = 1 : i32, name = "source", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.source"} {
        }
        obelisk.sv.symbol.net attributes {hierarchical_name = "untimed_always_startup.w", is_implicit = false, name = "w", net_kind = 1 : i32, node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.w"} {
        }
        // assign w = source;
        obelisk.sv.symbol.continuous_assign attributes {hierarchical_name = "untimed_always_startup", node_id = 7 : i64, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "untimed_always_startup.w", referenced_symbol = @s1.$root::@s3.untimed_always_startup::@s4.untimed_always_startup::@s6.w, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "untimed_always_startup.source", referenced_symbol = @s1.$root::@s3.untimed_always_startup::@s4.untimed_always_startup::@s5.source, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
        }
        // always begin end -- no timing control, so it never suspends.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "untimed_always_startup", node_id = 11 : i64, procedure_kind = 2 : i32, sym_name = "s11", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 12 : i64} {
            obelisk.sv.statement.list attributes {node_id = 13 : i64} {
            }
          }
        }
        // always @(source) ; -- starts by waiting on its event control.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "untimed_always_startup", node_id = 14 : i64, procedure_kind = 2 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 15 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 16 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "untimed_always_startup.source", referenced_symbol = @s1.$root::@s3.untimed_always_startup::@s4.untimed_always_startup::@s5.source, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 18 : i64} {
            }
          }
        }
      }
    }
  }
}

// An always procedure whose first statement is a timing control starts by
// waiting, so it is spawned before the continuous drivers to arm its event
// control. IEEE 1800-2017 9.2.2.1 gives an always procedure with no timing
// control no such suspension: it runs its body immediately, and IEEE
// 1800-2017 6.5 makes the net it reads one whose "resultant value of multiple
// drivers is determined by the resolution function of the net type", so those
// drivers must propagate before it starts.
// CHECK-LABEL: simulation.func @__obelisk_root
// CHECK: simulation.spawn @unit_2
// CHECK: simulation.spawn @unit_0
// CHECK: simulation.spawn @unit_1
// CHECK: simulation.return
