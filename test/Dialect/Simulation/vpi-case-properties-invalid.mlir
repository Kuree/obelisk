// RUN: talos-opt %s --verify-diagnostics -o /dev/null
module {
  simulation.design @bad {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    // expected-error @+2 {{VPI integer property value is outside its generated domain}}
    // expected-error @+1 {{failed to parse SimVPIPropertySetAttr parameter 'properties'}}
    simulation.statement.decl 1 in 1 scope 1 type 5 {vpi_properties = #simulation.vpi_properties<[#simulation.vpi_property<selector = 42 : i32, value = 4 : i32>]>}
  }
}
