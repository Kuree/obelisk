// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "wildcard_written_sensitivity", name = "wildcard_written_sensitivity", node_id = 0 : i64, sym_name = "s0.wildcard_written_sensitivity"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "wildcard_written_sensitivity", is_uninstantiated = false, name = "wildcard_written_sensitivity", node_id = 3 : i64, referenced_path = "wildcard_written_sensitivity", referenced_symbol = @s0.wildcard_written_sensitivity, sym_name = "s3.wildcard_written_sensitivity"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "wildcard_written_sensitivity", name = "wildcard_written_sensitivity", node_id = 4 : i64, sym_name = "s4.wildcard_written_sensitivity", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "wildcard_written_sensitivity.source", lifetime = 1 : i32, name = "source", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.source"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "wildcard_written_sensitivity.scratch", lifetime = 1 : i32, name = "scratch", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.scratch"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "wildcard_written_sensitivity.destination", lifetime = 1 : i32, name = "destination", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.destination"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "wildcard_written_sensitivity", node_id = 8 : i64, procedure_kind = 2 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 9 : i64} {
            obelisk.sv.timing.implicit_event attributes {node_id = 10 : i64} {
            }
            obelisk.sv.statement.block attributes {node_id = 11 : i64} {
              obelisk.sv.statement.list attributes {node_id = 12 : i64} {
                obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "wildcard_written_sensitivity.scratch", referenced_symbol = @s1.$root::@s3.wildcard_written_sensitivity::@s4.wildcard_written_sensitivity::@s6.scratch, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "wildcard_written_sensitivity.source", referenced_symbol = @s1.$root::@s3.wildcard_written_sensitivity::@s4.wildcard_written_sensitivity::@s5.source, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 17 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "wildcard_written_sensitivity.destination", referenced_symbol = @s1.$root::@s3.wildcard_written_sensitivity::@s4.wildcard_written_sensitivity::@s7.destination, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 20 : i64, referenced_path = "wildcard_written_sensitivity.scratch", referenced_symbol = @s1.$root::@s3.wildcard_written_sensitivity::@s4.wildcard_written_sensitivity::@s6.scratch, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

// IEEE 1800-2017 9.4.2.2 requires scratch in the implicit event expression
// because the statement reads it.  The process waits before evaluating the
// body, and its own body writes cannot activate the wait it reaches next.  The
// graph must retain the watch without adding a process-local sensitivity edge;
// a producer in another process can still activate it.
// CHECK-NOT: kind = sensitivity
// CHECK-LABEL: obelisk_sim.func private
// CHECK-SAME: entry_kind = 3 : i32
// CHECK-SAME: obelisk_sim.hierarchical_name = "wildcard_written_sensitivity"
// CHECK: obelisk_sim.suspend.any %[[SOURCE:[a-zA-Z0-9_]+]], %[[SCRATCH:[a-zA-Z0-9_]+]] edges [0, 0]
// CHECK-SAME: obelisk_sim.top_level_wildcard_wait
// CHECK: %[[VALUE:[0-9]+]] = obelisk_sim.ref.load %[[SOURCE]]
// CHECK: obelisk_sim.ref.store %[[VALUE]] to %[[SCRATCH]]
// CHECK: %[[FORWARD:[0-9]+]] = obelisk_sim.ref.load %[[SCRATCH]]
// CHECK: obelisk_sim.ref.store %[[FORWARD]] to %{{.*}}
// CHECK-NOT: obelisk.sv.
