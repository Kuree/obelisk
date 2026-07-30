// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_wired_resolution", name = "unsupported_wired_resolution", node_id = 0 : i64, sym_name = "s0.unsupported_wired_resolution"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unsupported_wired_resolution", is_uninstantiated = false, name = "unsupported_wired_resolution", node_id = 3 : i64, referenced_path = "unsupported_wired_resolution", referenced_symbol = @s0.unsupported_wired_resolution, sym_name = "s3.unsupported_wired_resolution"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unsupported_wired_resolution", name = "unsupported_wired_resolution", node_id = 4 : i64, sym_name = "s4.unsupported_wired_resolution"} {
        obelisk.sv.symbol.net attributes {hierarchical_name = "unsupported_wired_resolution.value", is_implicit = false, name = "value", net_kind = 2 : i32, node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.value"} {
        }
      }
    }
  }
}

// CHECK: unsupported net resolution kind wand
