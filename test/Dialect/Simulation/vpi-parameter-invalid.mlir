// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  obelisk_sim.design @value_without_type {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{immutable source type and value must be present together}}
    obelisk_sim.vpi_object.anchor @p id 0 type 41 in 0 ordinal 0
        hierarchy "p" debug "p" {
      immutable_value = #obelisk_sim.frozen_constant<
          value = [1 : i32, 0 : i32], isSigned = true> : i32
    }
  }
}

// -----

module {
  obelisk_sim.design @type_without_value {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{immutable source type and value must be present together}}
    obelisk_sim.vpi_object.anchor @p id 0 type 41 in 0 ordinal 0
        hierarchy "p" debug "p" {
      vpi_type = #obelisk_sim.vpi_type<kind = int, isSigned = true,
          isFourState = false, range = [31, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @immutable_non_parameter {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{immutable value is currently supported only on parameters}}
    obelisk_sim.vpi_object.anchor @event id 0 type 34 in 0 ordinal 0
        hierarchy "event" debug "event" {
      immutable_value = #obelisk_sim.frozen_constant<
          value = [1 : i32, 0 : i32], isSigned = true> : i32,
      vpi_type = #obelisk_sim.vpi_type<kind = int, isSigned = true,
          isFourState = false, range = [31, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @backed_parameter {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{scope backing requires a module, interface, or program anchor}}
    obelisk_sim.vpi_object.anchor @p id 0 type 41 in 0 ordinal 0
        hierarchy "p" debug "p" {
      backing = #obelisk_sim.vpi_backing<kind = scope, id = 0 : i64>,
      immutable_value = #obelisk_sim.frozen_constant<
          value = [1 : i32, 0 : i32], isSigned = true> : i32,
      vpi_type = #obelisk_sim.vpi_type<kind = int, isSigned = true,
          isFourState = false, range = [31, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @signedness_mismatch {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{immutable parameter value and VPI type signedness must agree}}
    obelisk_sim.vpi_object.anchor @p id 0 type 41 in 0 ordinal 0
        hierarchy "p" debug "p" {
      immutable_value = #obelisk_sim.frozen_constant<
          value = [1 : i32, 0 : i32], isSigned = true> : i32,
      vpi_type = #obelisk_sim.vpi_type<kind = int, isSigned = false,
          isFourState = false, range = [31, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  obelisk_sim.design @explicit_range_non_parameter {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{explicit parameter range marker requires a parameter anchor}}
    obelisk_sim.vpi_object.anchor @event id 0 type 34 in 0 ordinal 0
        hierarchy "event" debug "event" {has_explicit_parameter_range}
  }
}

// -----

module {
  obelisk_sim.design @explicit_range_without_value {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{explicit parameter range marker requires immutable type and value}}
    obelisk_sim.vpi_object.anchor @p id 0 type 41 in 0 ordinal 0
        hierarchy "p" debug "p" {has_explicit_parameter_range}
  }
}

// -----

module {
  obelisk_sim.design @explicit_range_scalar_root {
    obelisk_sim.scope.decl 0
    // expected-error @+1 {{explicit parameter range requires a packed-array semantic root}}
    obelisk_sim.vpi_object.anchor @p id 0 type 41 in 0 ordinal 0
        hierarchy "p" debug "p" {
      has_explicit_parameter_range,
      immutable_value = #obelisk_sim.frozen_constant<
          value = [1 : i32, 0 : i32], isSigned = true> : i32,
      vpi_type = #obelisk_sim.vpi_type<kind = int, isSigned = true,
          isFourState = false, range = [31, 0], children = [], childNames = []>
    }
  }
}
