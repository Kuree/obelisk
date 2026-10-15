// RUN: obelisk-opt %s --lower-obelisk-to-sim -verify-diagnostics -o /dev/null

// IEEE 1800-2023 23.3.3.7.1: an interconnect acquires its type from the typed
// ports it connects, so `interconnect n;` with no connection keeps none. That
// is diagnosed, and preparation stops before publishing leaf types.

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        // expected-error @+1 {{interconnect leaf 'top.n' has no typed net-port connection}}
        obelisk.sv.symbol.net @s5.n attributes {hierarchical_name = "top.n", is_implicit = false, name = "n", net_kind = 13 : i32, node_id = 5 : i64, semantic_type = !obelisk.untyped} {
        }
      }
    }
  }
}

