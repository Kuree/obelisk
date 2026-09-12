// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "primitive_strength", name = "primitive_strength", node_id = 0 : i64, sym_name = "s0.primitive_strength"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "primitive_strength", is_uninstantiated = false, name = "primitive_strength", node_id = 3 : i64, referenced_path = "primitive_strength", referenced_symbol = @s0.primitive_strength, sym_name = "s3.primitive_strength"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "primitive_strength", name = "primitive_strength", node_id = 4 : i64, sym_name = "s4.primitive_strength"} {
        obelisk.sv.symbol.net attributes {hierarchical_name = "primitive_strength.value", is_implicit = false, name = "value", net_kind = 12 : i32, node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.value"} {
        }
        obelisk.sv.symbol.net attributes {hierarchical_name = "primitive_strength.pull1", is_implicit = false, name = "pull1", net_kind = 1 : i32, node_id = 12 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s12.pull1"} {
        }
        obelisk.sv.symbol.net attributes {hierarchical_name = "primitive_strength.pull0", is_implicit = false, name = "pull0", net_kind = 1 : i32, node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s13.pull0"} {
        }
        obelisk.sv.symbol.net attributes {hierarchical_name = "primitive_strength.supply_pull1", is_implicit = false, name = "supply_pull1", net_kind = 1 : i32, node_id = 22 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s22.supply_pull1"} {
        }
        obelisk.sv.symbol.primitive_instance attributes {drive_strength0 = 2 : i32, drive_strength1 = 3 : i32, hierarchical_name = "primitive_strength", node_id = 6 : i64, primitive_name = "bufif1", sym_name = "s6", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {node_id = 8 : i64, referenced_path = "primitive_strength.value", referenced_symbol = @s1.$root::@s3.primitive_strength::@s4.primitive_strength::@s5.value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.empty_argument attributes {node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
          }
          obelisk.sv.expression.integer_literal attributes {constant_value = "1'bx", node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
          }
        }
        obelisk.sv.symbol.primitive_instance attributes {hierarchical_name = "primitive_strength", node_id = 14 : i64, primitive_name = "pullup", sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 15 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "primitive_strength.pull1", referenced_symbol = @s1.$root::@s3.primitive_strength::@s4.primitive_strength::@s12.pull1, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.empty_argument attributes {node_id = 17 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
        }
        obelisk.sv.symbol.primitive_instance attributes {drive_strength0 = 3 : i32, drive_strength1 = 0 : i32, hierarchical_name = "primitive_strength", node_id = 23 : i64, primitive_name = "pullup", sym_name = "s23", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 24 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "primitive_strength.supply_pull1", referenced_symbol = @s1.$root::@s3.primitive_strength::@s4.primitive_strength::@s22.supply_pull1, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.empty_argument attributes {node_id = 26 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
        }
        obelisk.sv.symbol.primitive_instance attributes {drive_strength0 = 3 : i32, drive_strength1 = 1 : i32, hierarchical_name = "primitive_strength", node_id = 18 : i64, primitive_name = "pulldown", sym_name = "s18", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 19 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_path = "primitive_strength.pull0", referenced_symbol = @s1.$root::@s3.primitive_strength::@s4.primitive_strength::@s13.pull0, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.empty_argument attributes {node_id = 21 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
        }
        // A multi-output gate has one assignment expression per output.
        // vpiSize counts only the non-assignment input terminals.
        obelisk.sv.symbol.primitive_instance attributes {hierarchical_name = "primitive_strength.multi", name = "multi", node_id = 60 : i64, primitive_name = "buf", sym_name = "s60.multi", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 61 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {node_id = 62 : i64, referenced_path = "primitive_strength.value", referenced_symbol = @s1.$root::@s3.primitive_strength::@s4.primitive_strength::@s5.value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.empty_argument attributes {node_id = 63 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 64 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {node_id = 65 : i64, referenced_path = "primitive_strength.value", referenced_symbol = @s1.$root::@s3.primitive_strength::@s4.primitive_strength::@s5.value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.empty_argument attributes {node_id = 66 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 67 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
          }
        }
      }
    }
  }
}

// IEEE 1800-2017 28.12.2 requires an uncertain conditional-gate output to
// retain L/H ranges. The low and high polarity banks encode those ranges with
// asymmetric high-impedance strengths.
// CHECK: obelisk_sim.vpi_object.anchor {{.*}} type 21 {{.*}} hierarchy "primitive_strength.multi" debug "multi" {{.*}}primitive_input_count = 1 : i64
// CHECK: obelisk_sim.net.decl {{[0-9]+}} in {{[0-9]+}} : !obelisk_sim.logic<1> design
// CHECK-SAME: resolution_kind = 2 : i32
// CHECK: obelisk_sim.driver.decl {{[0-9]+}} in {{[0-9]+}} drives {{[0-9]+}} : !obelisk_sim.logic<1> design
// CHECK-SAME: obelisk_sim.strength_bank = 0 : i32
// CHECK-SAME: obelisk_sim.strength_group = [[GROUP:[0-9]+]] : i64
// CHECK-SAME: strength0 = 5 : i32
// CHECK-SAME: strength1 = 0 : i32
// CHECK: obelisk_sim.driver.decl {{[0-9]+}} in {{[0-9]+}} drives {{[0-9]+}} : !obelisk_sim.logic<1> design
// CHECK-SAME: obelisk_sim.strength_bank = 1 : i32
// CHECK-SAME: obelisk_sim.strength_group = [[GROUP]] : i64
// CHECK-SAME: strength0 = 0 : i32
// CHECK-SAME: strength1 = 3 : i32
// IEEE 1800-2017 28.3.2 and 28.10: an unspecified pull source uses pull
// strength for both declaration polarities; only the driven polarity matters,
// and an explicit pulldown strength0 replaces that polarity.
// CHECK: obelisk_sim.driver.decl {{[0-9]+}} in {{[0-9]+}} drives {{[0-9]+}} : !obelisk_sim.logic<1> design
// CHECK-SAME: strength0 = 5 : i32
// CHECK-SAME: strength1 = 5 : i32
// CHECK: obelisk_sim.driver.decl {{[0-9]+}} in {{[0-9]+}} drives {{[0-9]+}} : !obelisk_sim.logic<1> design
// CHECK-SAME: strength0 = 5 : i32
// CHECK-SAME: strength1 = 7 : i32
// CHECK: obelisk_sim.driver.decl {{[0-9]+}} in {{[0-9]+}} drives {{[0-9]+}} : !obelisk_sim.logic<1> design
// CHECK-SAME: strength0 = 3 : i32
// CHECK-SAME: strength1 = 5 : i32
// CHECK: obelisk_sim.driver.drive
// CHECK-SAME: obelisk_sim.defer_net_resolution
// CHECK: obelisk_sim.driver.drive
// CHECK: obelisk_sim.logic.constant true, false
// CHECK: obelisk_sim.driver.drive
// CHECK: obelisk_sim.logic.constant true, false
// CHECK: obelisk_sim.driver.drive
// CHECK: obelisk_sim.logic.constant false, false
// CHECK: obelisk_sim.driver.drive
