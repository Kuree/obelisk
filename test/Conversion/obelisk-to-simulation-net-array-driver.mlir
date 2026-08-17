// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 10.3: an element of an unpacked array of nets is a legal
// continuous-assignment lvalue. Each assignment gets its own driver covering
// that element's run of the array, addressed by provenance span rather than
// packed width -- an unpacked array has no packed width at all. `n[1]` is the
// array's first element in declaration order, so it holds the lower run.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "net_array_driver", name = "net_array_driver", node_id = 0 : i64, sym_name = "s0.net_array_driver"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "net_array_driver", is_uninstantiated = false, name = "net_array_driver", node_id = 3 : i64, referenced_path = "net_array_driver", referenced_symbol = @s0.net_array_driver, sym_name = "s3.net_array_driver"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "net_array_driver", name = "net_array_driver", node_id = 4 : i64, sym_name = "s4.net_array_driver", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.net attributes {hierarchical_name = "net_array_driver.n", is_implicit = false, name = "n", net_kind = 1 : i32, node_id = 5 : i64, semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "s5.n"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "net_array_driver.d", lifetime = 1 : i32, name = "d", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>, sym_name = "s6.d"} {
        }
        obelisk.sv.symbol.continuous_assign attributes {hierarchical_name = "net_array_driver", node_id = 7 : i64, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 8 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
            obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 9 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "net_array_driver.n", referenced_symbol = @s1.$root::@s3.net_array_driver::@s4.net_array_driver::@s5.n, semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "net_array_driver.d", referenced_symbol = @s1.$root::@s3.net_array_driver::@s4.net_array_driver::@s6.d, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
            }
          }
        }
        obelisk.sv.symbol.continuous_assign attributes {hierarchical_name = "net_array_driver", node_id = 13 : i64, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
            obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 15 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "net_array_driver.n", referenced_symbol = @s1.$root::@s3.net_array_driver::@s4.net_array_driver::@s5.n, semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "net_array_driver.d", referenced_symbol = @s1.$root::@s3.net_array_driver::@s4.net_array_driver::@s6.d, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
            }
          }
        }
      }
    }
  }
}


// CHECK: obelisk_sim.net.decl [[NET:[0-9]+]] in {{[0-9]+}} : !obelisk_sim.unpacked_array<1 : 0 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>
// CHECK-SAME: hierarchy "net_array_driver.n"

// CHECK-DAG: obelisk_sim.driver.decl {{[0-9]+}} in {{[0-9]+}} drives [[NET]] {{.*}} {driven_low = 0 : i64, driven_width = 2 : i64}
// CHECK-DAG: obelisk_sim.driver.decl {{[0-9]+}} in {{[0-9]+}} drives [[NET]] {{.*}} {driven_low = 2 : i64, driven_width = 2 : i64}
