// RUN: talos-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  simulation.design @mixed_owner_cycle {
    simulation.scope.decl 0
    simulation.class.decl @runtime id 1 {is_abstract = false, is_final = false, is_interface = false}
    // expected-error @+1 {{lexical parent relation contains a cycle}}
    simulation.vpi_object.anchor @definition id 0 type 652 in 0 parent @canonical ordinal 0 hierarchy "C" debug "C"
    simulation.vpi_typespec.decl @canonical id 0 in 0 owner @definition hierarchy "C" debug "C" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false, symbol = @runtime, range = [], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @typedef_is_not_a_scope {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_typespec.decl @alias id 0 in 0 owner @package hierarchy "pkg::word" debug "word" {
      target_type = #simulation.vpi_type<kind = reg, isSigned = false, isFourState = true, range = [7, 0], children = [], childNames = []>
    }
    // expected-error @+1 {{references an unknown VPI parent anchor}}
    simulation.vpi_object.anchor @method id 1 type 20 in 0 parent @alias ordinal 0 hierarchy "pkg::word::method" debug "method"
  }
}

// -----

module {
  simulation.design @typedef_cannot_own_typespec {
    simulation.scope.decl 0
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0 hierarchy "pkg" debug "pkg"
    simulation.vpi_typespec.decl @alias id 0 in 0 owner @package hierarchy "pkg::word" debug "word" {
      target_type = #simulation.vpi_type<kind = reg, isSigned = false, isFourState = true, range = [7, 0], children = [], childNames = []>
    }
    // expected-error @+1 {{typespec must be owned by a VPI anchor}}
    simulation.vpi_typespec.decl @nested id 1 in 0 owner @alias hierarchy "pkg::word::nested" debug "nested" {
      target_type = #simulation.vpi_type<kind = reg, isSigned = false, isFourState = true, range = [7, 0], children = [], childNames = []>
    }
  }
}
