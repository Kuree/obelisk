// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "always_comb_written_sensitivity", name = "always_comb_written_sensitivity", node_id = 0 : i64, sym_name = "s0.always_comb_written_sensitivity"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "always_comb_written_sensitivity", is_uninstantiated = false, name = "always_comb_written_sensitivity", node_id = 3 : i64, referenced_path = "always_comb_written_sensitivity", referenced_symbol = @s0.always_comb_written_sensitivity, sym_name = "s3.always_comb_written_sensitivity"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "always_comb_written_sensitivity", name = "always_comb_written_sensitivity", node_id = 4 : i64, sym_name = "s4.always_comb_written_sensitivity", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "always_comb_written_sensitivity.source", lifetime = 1 : i32, name = "source", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.source"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "always_comb_written_sensitivity.scratch", lifetime = 1 : i32, name = "scratch", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.scratch"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "always_comb_written_sensitivity.destination", lifetime = 1 : i32, name = "destination", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.destination"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "always_comb_written_sensitivity", node_id = 8 : i64, procedure_kind = 3 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 9 : i64} {
            obelisk.sv.statement.list attributes {node_id = 10 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "always_comb_written_sensitivity.scratch", referenced_symbol = @s1.$root::@s3.always_comb_written_sensitivity::@s4.always_comb_written_sensitivity::@s6.scratch, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "always_comb_written_sensitivity.source", referenced_symbol = @s1.$root::@s3.always_comb_written_sensitivity::@s4.always_comb_written_sensitivity::@s5.source, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "always_comb_written_sensitivity.destination", referenced_symbol = @s1.$root::@s3.always_comb_written_sensitivity::@s4.always_comb_written_sensitivity::@s7.destination, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "always_comb_written_sensitivity.scratch", referenced_symbol = @s1.$root::@s3.always_comb_written_sensitivity::@s4.always_comb_written_sensitivity::@s6.scratch, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

// IEEE 1800-2017 9.2.2.2.1 excludes expressions written within an
// always_comb from its inferred sensitivity.  Scratch is still loaded and
// therefore remains a read effect, but only source may arm the process.
// CHECK-LABEL: obelisk_sim.func private
// CHECK-SAME: entry_kind = 4 : i32
// CHECK-SAME: obelisk_sim.hierarchical_name = "always_comb_written_sensitivity"
// CHECK: %[[VALUE:[0-9]+]] = obelisk_sim.ref.load %[[SOURCE:[a-zA-Z0-9_]+]]
// CHECK: obelisk_sim.ref.store %[[VALUE]] to %[[SCRATCH:[a-zA-Z0-9_]+]]
// CHECK: %[[FORWARD:[0-9]+]] = obelisk_sim.ref.load %[[SCRATCH]]
// CHECK: obelisk_sim.ref.store %[[FORWARD]] to %{{.*}}
// CHECK: obelisk_sim.suspend.change %[[SOURCE]]
// CHECK-NOT: obelisk_sim.suspend.change %[[SCRATCH]]
// CHECK-NOT: obelisk.sv.
