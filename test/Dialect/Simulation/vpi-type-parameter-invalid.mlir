// RUN: talos-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  simulation.design @missing_type {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{type parameter requires a source type and no immutable value}}
    simulation.vpi_object.anchor @type id 1 type 609 in 0 parent @package ordinal 0 hierarchy "pkg.T" debug "T"
  }
}

// -----

module {
  simulation.design @type_with_value {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{type parameter requires a source type and no immutable value}}
    simulation.vpi_object.anchor @type id 1 type 609 in 0 parent @package ordinal 0 hierarchy "pkg.T" debug "T" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false,
          range = [31, 0], children = [], childNames = []>,
      immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32
    }
  }
}

// -----

module {
  simulation.design @unknown_alias {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    // expected-error @+1 {{VPI typedef alias @missing does not reference a typedef typespec declaration}}
    simulation.vpi_object.anchor @type id 1 type 609 in 0 parent @package ordinal 0 hierarchy "pkg.T" debug "T" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false,
          range = [31, 0], children = [], childNames = [], typedefAliases = [@missing]>
    }
  }
}
