// RUN: obelisk-opt %s --obelisk-sim-materialize-dpi-exports \
// RUN:   --verify-diagnostics

module attributes {obelisk_sim.has_dpi_exports} {
  obelisk_sim.design @design {
    obelisk_sim.scope.decl 0 hierarchy "design"
    obelisk_sim.code_unit.decl 10 in 0 function hierarchy "design.foo"
    obelisk_sim.code_unit.decl 11 in 0 function hierarchy "design.collision"

    // expected-error@+1 {{DPI export bridge symbol already exists}}
    obelisk_sim.func @foo(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {
          code_unit_id = 10 : i64, entry_kind = 8 : i32,
          obelisk_sim.dpi_export,
          obelisk_sim.dpi_c_identifier = "foo",
          obelisk_sim.dpi_export_id = 1 : i32,
          obelisk_sim.dpi_scope_id = 0 : i64,
          obelisk_sim.dpi_logical_inputs = 0 : i32,
          obelisk_sim.dpi_abi_signature = []
        } {
      obelisk_sim.return
    }

    obelisk_sim.func private @"foo.__obelisk_dpi_export_bridge"(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 11 : i64, entry_kind = 8 : i32} {
      obelisk_sim.return
    }
  }
}
