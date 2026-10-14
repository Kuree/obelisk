// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

func.func private @inverted() attributes {
  // expected-error @+1 {{integer range is empty}}
  ranges = [#simulation.integer_range<umin = 5 : i8, umax = 3 : i8,
                                      smin = 0 : i8, smax = 127 : i8>]}

// -----

func.func private @constant_width() attributes {
  // expected-error @+1 {{constant integer expression needs only a value of its width}}
  exprs = [#simulation.integer_expression<kind = constant, width = 8,
                                          value = 0 : i16>]}

// -----

func.func private @arity() attributes {
  // expected-error @+1 {{integer expression has malformed operands}}
  exprs = [#simulation.integer_expression<kind = add, width = 8,
                                          operands = []>]}

// -----

// expected-error @+1 {{attribute 'simulation.result_ranges' must contain #simulation.integer_range or unit entries}}
func.func private @untyped_ranges() attributes {
  simulation.result_ranges = [{umin = 0 : i8}]}

// -----

// expected-error @+1 {{attribute 'simulation.integer_range' belongs on an argument}}
func.func private @range_on_function() attributes {
  simulation.integer_range = #simulation.integer_range<umin = 0 : i8,
      umax = 1 : i8, smin = 0 : i8, smax = 1 : i8>}

// -----

// expected-error @+1 {{attribute 'simulation.gc_polled' belongs on a module}}
func.func private @polled() attributes {simulation.gc_polled}

// -----

// expected-error @+1 {{attribute 'simulation.boolean_minimizations' must contain #simulation.boolean_minimization entries}}
func.func private @untyped_minimizations() attributes {
  simulation.boolean_minimizations = [{scope = "antecedent"}]}

// -----

// expected-error @+1 {{attribute 'simulation.random_mode_index' has the wrong payload kind}}
func.func private @wrong_payload() attributes {
  simulation.random_mode_index = "first"}

// -----

// expected-error @+1 {{has undeclared attribute 'simulation.future_semantics'}}
func.func private @undeclared() attributes {simulation.future_semantics}
