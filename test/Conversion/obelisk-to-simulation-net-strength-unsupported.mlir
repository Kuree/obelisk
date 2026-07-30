// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_strength", name = "unsupported_strength", node_id = 0 : i64, sym_name = "s0.unsupported_strength"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unsupported_strength", is_uninstantiated = false, name = "unsupported_strength", node_id = 3 : i64, referenced_path = "unsupported_strength", referenced_symbol = @s0.unsupported_strength, sym_name = "s3.unsupported_strength"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unsupported_strength", name = "unsupported_strength", node_id = 4 : i64, sym_name = "s4.unsupported_strength"} {
        obelisk.sv.symbol.net attributes {hierarchical_name = "unsupported_strength.value", is_implicit = false, name = "value", net_kind = 1 : i32, node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.value", unsupported_strength = "Pull,Strong"} {
          obelisk.sv.expression.conversion attributes {node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
            }
          }
        }
      }
    }
  }
}

// CHECK: net strengths are not supported: Pull,Strong
