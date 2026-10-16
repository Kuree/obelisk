// RUN: obelisk-opt %s -split-input-file -verify-diagnostics -o /dev/null

module {
  simulation.design @design {
    simulation.scope.decl 0 hierarchy "$root"
    // expected-error @+1 {{constraint requires a named class-owned declaration}}
    simulation.vpi_object.anchor @constraint id 0 type 653 in 0 ordinal 0 hierarchy "c" debug "c"
  }
}

// -----

module {
  simulation.design @design {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0 hierarchy "p" debug "p"
    // expected-error @+1 {{has an illegal lexical parent kind}}
    simulation.vpi_object.anchor @constraint id 1 type 653 in 0 parent @package ordinal 0 hierarchy "p::c" debug "c"
  }
}

// -----

module {
  simulation.design @design {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0 hierarchy "$unit" debug "$unit" {is_compilation_unit}
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @package ordinal 0 hierarchy "C" debug "C"
    // expected-error @+1 {{constraint requires a named class-owned declaration}}
    simulation.vpi_object.anchor @constraint id 2 type 653 in 0 parent @class ordinal 0 hierarchy "C::c" debug ""
  }
}

// -----

module {
  simulation.design @design {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0 hierarchy "$unit" debug "$unit" {is_compilation_unit}
    simulation.vpi_object.anchor @class id 1 type 652 in 0 parent @package ordinal 0 hierarchy "C" debug "C"
    simulation.vpi_object.anchor @first id 2 type 653 in 0 parent @class ordinal 0 hierarchy "C::c" debug "c"
    // expected-error @+1 {{duplicates a constraint declaration name in the same class}}
    simulation.vpi_object.anchor @second id 3 type 653 in 0 parent @class ordinal 1 hierarchy "C::c" debug "c"
  }
}
