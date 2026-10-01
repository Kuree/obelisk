// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.unsupported_strength attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_strength", name = "unsupported_strength", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.unsupported_strength attributes {hierarchical_name = "unsupported_strength", is_uninstantiated = false, name = "unsupported_strength", node_id = 3 : i64, referenced_path = "unsupported_strength", referenced_symbol = @s0.unsupported_strength} {
      obelisk.sv.symbol.instance_body @s4.unsupported_strength attributes {hierarchical_name = "unsupported_strength", name = "unsupported_strength", node_id = 4 : i64} {
        obelisk.sv.symbol.net @s5.value attributes {drive_strength0 = 2 : i32, drive_strength1 = 3 : i32, hierarchical_name = "unsupported_strength.value", is_implicit = false, name = "value", net_kind = 1 : i32, node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
          obelisk.sv.expression.conversion attributes {node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
            }
          }
        }
      }
    }
  }
}

// IEEE 1800-2017 6.3.2.2 makes this declaration initializer a continuous
// assignment with the declaration's drive strengths.
// CHECK: simulation.driver.decl {{[0-9]+}} in {{[0-9]+}} drives {{[0-9]+}} : !simulation.logic<1> design
// CHECK-SAME: strength0 = 5 : i32
// CHECK-SAME: strength1 = 3 : i32
