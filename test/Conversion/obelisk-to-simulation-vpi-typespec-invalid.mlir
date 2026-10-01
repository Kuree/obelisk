// RUN: obelisk-opt %s --split-input-file '--lower-obelisk-to-sim=opt-level=0' --verify-diagnostics -o /dev/null

module {
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 0 : i64} {
    obelisk.sv.symbol.compilation_unit @cu attributes {
        hierarchical_name = "$unit", node_id = 1 : i64} {
      // expected-error @+1 {{VPI typedef is missing a hierarchy or debug name}}
      obelisk.sv.type.type_alias @bad_t attributes {
          hierarchical_name = "", name = "bad_t", node_id = 2 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
      } {}
    }
  }
}

// -----

module {
  obelisk.sv.symbol.definition @wrapper_def attributes {definition_kind = 0 : i32,
      hierarchical_name = "wrapper", name = "wrapper", node_id = 0 : i64
  } {}
  obelisk.sv.symbol.definition @iface_def attributes {definition_kind = 1 : i32,
      hierarchical_name = "iface", name = "iface", node_id = 1 : i64
  } {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 2 : i64} {
    obelisk.sv.symbol.instance @left_wrapper attributes {hierarchical_name = "left",
        is_uninstantiated = false, name = "left", node_id = 3 : i64,
        referenced_path = "wrapper", referenced_symbol = @wrapper_def
    } {
      obelisk.sv.symbol.instance_body @same_body attributes {hierarchical_name = "left",
          name = "wrapper", node_id = 4 : i64} {
        obelisk.sv.symbol.instance @same_iface attributes {
            hierarchical_name = "left.iface", is_uninstantiated = false,
            is_virtual_interface_type_instance = true, name = "iface",
            node_id = 5 : i64, referenced_path = "iface",
            referenced_symbol = @iface_def} {}
      }
    }
    obelisk.sv.symbol.instance @right_wrapper attributes {hierarchical_name = "right",
        is_uninstantiated = false, name = "right", node_id = 6 : i64,
        referenced_path = "wrapper", referenced_symbol = @wrapper_def
    } {
      obelisk.sv.symbol.instance_body @same_body attributes {hierarchical_name = "right",
          name = "wrapper", node_id = 7 : i64} {
        // expected-error @+1 {{VPI interface identity collides with another semantic instance @root::@same_body::@same_iface}}
        obelisk.sv.symbol.instance @same_iface attributes {
            hierarchical_name = "right.iface", is_uninstantiated = false,
            is_virtual_interface_type_instance = true, name = "iface",
            node_id = 8 : i64, referenced_path = "iface",
            referenced_symbol = @iface_def} {}
      }
    }
  }
}
