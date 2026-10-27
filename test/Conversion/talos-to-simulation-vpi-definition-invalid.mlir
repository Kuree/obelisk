// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' --verify-diagnostics -o /dev/null

module {
  talos.sv.symbol.definition @cell_def attributes {definition_kind = 0 : i32,
      hierarchical_name = "cell", name = "cell", node_id = 0 : i64
  } {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.instance @left_i attributes {hierarchical_name = "left",
        is_uninstantiated = false, name = "left", node_id = 2 : i64,
        referenced_path = "cell", referenced_symbol = @cell_def
    } {
      talos.sv.symbol.instance_body @left_b attributes {hierarchical_name = "left",
          name = "left", node_id = 3 : i64,
          simulation.vpi_definition_name = "cell"} {}
    }
    talos.sv.symbol.instance @right_i attributes {hierarchical_name = "right",
        is_uninstantiated = false, name = "right", node_id = 4 : i64,
        referenced_path = "cell", referenced_symbol = @cell_def
    } {
      // expected-error @+1 {{instances of one source definition disagree on the VPI definition name}}
      talos.sv.symbol.instance_body @right_b attributes {hierarchical_name = "right",
          name = "right", node_id = 5 : i64,
          simulation.vpi_definition_name = "not_cell"} {}
    }
  }
}
