// RUN: obelisk-opt %s --split-input-file '--lower-obelisk-to-sim=opt-level=0' --verify-diagnostics -o /dev/null

module {
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 0 : i64, sym_name = "root"} {
    obelisk.sv.symbol.compilation_unit attributes {
        hierarchical_name = "$unit", node_id = 1 : i64, sym_name = "cu"} {
      // expected-error @+1 {{VPI typedef is missing a hierarchy or debug name}}
      obelisk.sv.type.type_alias attributes {
          hierarchical_name = "", name = "bad_t", node_id = 2 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
          sym_name = "bad_t"} {}
    }
  }
}

// -----

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "wrapper", name = "wrapper", node_id = 0 : i64,
      sym_name = "wrapper_def"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32,
      hierarchical_name = "iface", name = "iface", node_id = 1 : i64,
      sym_name = "iface_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 2 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "left",
        is_uninstantiated = false, name = "left", node_id = 3 : i64,
        referenced_path = "wrapper", referenced_symbol = @wrapper_def,
        sym_name = "left_wrapper"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "left",
          name = "wrapper", node_id = 4 : i64, sym_name = "same_body"} {
        obelisk.sv.symbol.instance attributes {
            hierarchical_name = "left.iface", is_uninstantiated = false,
            is_virtual_interface_type_instance = true, name = "iface",
            node_id = 5 : i64, referenced_path = "iface",
            referenced_symbol = @iface_def, sym_name = "same_iface"} {}
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "right",
        is_uninstantiated = false, name = "right", node_id = 6 : i64,
        referenced_path = "wrapper", referenced_symbol = @wrapper_def,
        sym_name = "right_wrapper"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "right",
          name = "wrapper", node_id = 7 : i64, sym_name = "same_body"} {
        // expected-error @+1 {{VPI interface identity collides with another semantic instance @root::@same_body::@same_iface}}
        obelisk.sv.symbol.instance attributes {
            hierarchical_name = "right.iface", is_uninstantiated = false,
            is_virtual_interface_type_instance = true, name = "iface",
            node_id = 8 : i64, referenced_path = "iface",
            referenced_symbol = @iface_def, sym_name = "same_iface"} {}
      }
    }
  }
}
