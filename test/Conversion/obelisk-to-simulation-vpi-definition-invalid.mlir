// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' --verify-diagnostics -o /dev/null

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "cell", name = "cell", node_id = 0 : i64,
      sym_name = "cell_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "left",
        is_uninstantiated = false, name = "left", node_id = 2 : i64,
        referenced_path = "cell", referenced_symbol = @cell_def,
        sym_name = "left_i"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "left",
          name = "left", node_id = 3 : i64, sym_name = "left_b",
          obelisk_sim.vpi_definition_name = "cell"} {}
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "right",
        is_uninstantiated = false, name = "right", node_id = 4 : i64,
        referenced_path = "cell", referenced_symbol = @cell_def,
        sym_name = "right_i"} {
      // expected-error @+1 {{instances of one source definition disagree on the VPI definition name}}
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "right",
          name = "right", node_id = 5 : i64, sym_name = "right_b",
          obelisk_sim.vpi_definition_name = "not_cell"} {}
    }
  }
}
