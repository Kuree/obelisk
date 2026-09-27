// RUN: obelisk-opt %s --obelisk-sim-materialize-dpi-exports \
// RUN:   --verify-diagnostics

module attributes {simulation.has_dpi_exports} {
  simulation.design @design {
    simulation.scope.decl 0 hierarchy "design"
    simulation.code_unit.decl 10 in 0 function hierarchy "design.foo"
    simulation.code_unit.decl 11 in 0 function hierarchy "design.collision"

    // expected-error@+1 {{DPI export bridge symbol already exists}}
    simulation.func @foo(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {
          code_unit_id = 10 : i64, entry_kind = 8 : i32,
          simulation.dpi_export,
          simulation.dpi_c_identifier = "foo",
          simulation.dpi_export_id = 1 : i32,
          simulation.dpi_scope_id = 0 : i64,
          simulation.dpi_logical_inputs = 0 : i32,
          simulation.dpi_abi_signature = []
        } {
      simulation.return
    }

    simulation.func private @"foo.__obelisk_dpi_export_bridge"(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 11 : i64, entry_kind = 8 : i32} {
      simulation.return
    }
  }
}
