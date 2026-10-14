// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

// expected-error @+1 {{has undeclared attribute 'coverage.config_text'}}
module attributes {coverage.config_text = "x"} {}

// -----

// expected-error @+1 {{attribute 'coverage.metrics' must contain only strings}}
module attributes {coverage.metrics = [1]} {}

// -----

// expected-error @+1 {{attribute 'coverage.line_point_count' belongs on a module}}
func.func private @count() attributes {coverage.line_point_count = 1 : i64}

// -----

// expected-error @+1 {{attribute 'coverage.toggle_bindings' must contain only #coverage.toggle_binding entries}}
func.func private @bindings() attributes {
  coverage.toggle_bindings = [{base = 0 : i64, low = 0 : i64, width = 1 : i64}]}

// -----

func.func private @binding() attributes {
  // expected-error @+1 {{toggle binding must cover at least one bit}}
  coverage.toggle_bindings = [#coverage.toggle_binding<base = 0, low = 0, width = 0>]}
