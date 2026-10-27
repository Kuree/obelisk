// RUN: talos-opt %s --split-input-file --verify-diagnostics
module {
  simulation.design @bad {
    // expected-error @+1 {{VPI definition kind must be a module, interface, program, gate, or switch}}
    simulation.vpi_definition.decl @package type 600 name "package"
    simulation.vpi_definition_member.decl @actual of @package type 40 ordinal 0 name "" {unavailable_rhs}
  }
}
// -----
module {
  simulation.design @bad {
    simulation.vpi_definition.decl @program type 602 name "program"
    // Programs cannot instantiate modules, interfaces or other programs (24.3).
    // This direct-child shared representation therefore does not apply.
    // expected-error @+1 {{shared parameter assignments require a module or interface definition}}
    simulation.vpi_definition_member.decl @actual of @program type 40 ordinal 0 name "" {unavailable_rhs}
  }
}
