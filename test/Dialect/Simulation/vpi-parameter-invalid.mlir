// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  simulation.design @value_without_type {
    simulation.scope.decl 0
    // expected-error @+1 {{immutable source type and value must be present together}}
    simulation.vpi_object.anchor @p id 0 type 41 in 0 ordinal 0
        hierarchy "p" debug "p" {
      immutable_value = #simulation.frozen_constant<
          value = [1 : i32, 0 : i32], isSigned = true> : i32
    }
  }
}

// -----

module {
  simulation.design @type_without_value {
    simulation.scope.decl 0
    // expected-error @+1 {{immutable source type and value must be present together}}
    simulation.vpi_object.anchor @p id 0 type 41 in 0 ordinal 0
        hierarchy "p" debug "p" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true,
          isFourState = false, range = [31, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @immutable_non_parameter {
    simulation.scope.decl 0
    // expected-error @+1 {{immutable value is currently supported only on parameters}}
    simulation.vpi_object.anchor @event id 0 type 34 in 0 ordinal 0
        hierarchy "event" debug "event" {
      immutable_value = #simulation.frozen_constant<
          value = [1 : i32, 0 : i32], isSigned = true> : i32,
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true,
          isFourState = false, range = [31, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @backed_parameter {
    simulation.scope.decl 0
    // expected-error @+1 {{scope backing requires a module, interface, or program anchor}}
    simulation.vpi_object.anchor @p id 0 type 41 in 0 ordinal 0
        hierarchy "p" debug "p" {
      backing = #simulation.vpi_backing<kind = scope, id = 0 : i64>,
      immutable_value = #simulation.frozen_constant<
          value = [1 : i32, 0 : i32], isSigned = true> : i32,
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true,
          isFourState = false, range = [31, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @signedness_mismatch {
    simulation.scope.decl 0
    // expected-error @+1 {{immutable parameter value and VPI type signedness must agree}}
    simulation.vpi_object.anchor @p id 0 type 41 in 0 ordinal 0
        hierarchy "p" debug "p" {
      immutable_value = #simulation.frozen_constant<
          value = [1 : i32, 0 : i32], isSigned = true> : i32,
      vpi_type = #simulation.vpi_type<kind = int, isSigned = false,
          isFourState = false, range = [31, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @explicit_range_non_parameter {
    simulation.scope.decl 0
    // expected-error @+1 {{explicit parameter range marker requires a parameter anchor}}
    simulation.vpi_object.anchor @event id 0 type 34 in 0 ordinal 0
        hierarchy "event" debug "event" {has_explicit_parameter_range}
  }
}

// -----

module {
  simulation.design @explicit_range_without_value {
    simulation.scope.decl 0
    // expected-error @+1 {{explicit parameter range marker requires immutable type and value}}
    simulation.vpi_object.anchor @p id 0 type 41 in 0 ordinal 0
        hierarchy "p" debug "p" {has_explicit_parameter_range}
  }
}

// -----

module {
  simulation.design @explicit_range_scalar_root {
    simulation.scope.decl 0
    // expected-error @+1 {{explicit parameter range requires a packed-array semantic root}}
    simulation.vpi_object.anchor @p id 0 type 41 in 0 ordinal 0
        hierarchy "p" debug "p" {
      has_explicit_parameter_range,
      immutable_value = #simulation.frozen_constant<
          value = [1 : i32, 0 : i32], isSigned = true> : i32,
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true,
          isFourState = false, range = [31, 0], children = [], childNames = []>
    }
  }
}
