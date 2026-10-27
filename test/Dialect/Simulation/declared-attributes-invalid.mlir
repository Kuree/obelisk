// RUN: talos-opt %s --split-input-file --verify-diagnostics

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

// -----

func.func private @step_mode() attributes {
  // expected-error @+1 {{random object step mode index is out of range}}
  steps = [#simulation.random_object_step<field = @f,
    storage = !simulation.class_handle<@C>, modeIndex = 64>]}

// -----

func.func private @untyped_path() attributes {
  // expected-error @+1 {{nested random object path must contain #simulation.random_object_step entries with a concrete class and mode index}}
  object = #simulation.random_nested_object<field = @f,
    concrete = !simulation.class_handle<@C>,
    storage = !simulation.class_handle<@C>, outerModeIndex = 0,
    path = [#simulation.random_object_step<field = @g,
      storage = !simulation.class_handle<@C>>]>}

// -----

func.func private @guard_depth() attributes {
  // expected-error @+1 {{random alias guard depth must index its path}}
  guard = #simulation.random_alias_guard<
    object = #simulation.random_nested_object<field = @f,
      concrete = !simulation.class_handle<@C>,
      storage = !simulation.class_handle<@C>, outerModeIndex = 0>,
    aliasDepth = 0>}

// -----

// expected-error @+1 {{attribute 'simulation.random_nested_hooks' must contain #simulation.random_nested_hooks entries}}
func.func private @untyped_hooks() attributes {
  simulation.random_nested_hooks = [{pre_callee = @f}]}

// -----

func.func private @property_target() attributes {
  // expected-error @+1 {{random property must name exactly one field or path}}
  property = #simulation.random_property<field = @f, path = "top.f",
    type = i1, width = 1, modeIndex = 0, isSigned = false, isRandC = false>}

// -----

func.func private @mixed_cycle() attributes {
  // expected-error @+1 {{randc state must name three class fields or three storage paths}}
  cycle = #simulation.random_cycle_state<key = @k, position = "top.p",
                                         domain = @d>}

// -----

func.func private @static_cycle_on_field() attributes {
  // expected-error @+1 {{randc state must match its variable}}
  property = #simulation.random_property<field = @f, type = i1, width = 1,
    modeIndex = 0, isSigned = false, isRandC = true,
    cycle = #simulation.random_cycle_state<key = "k", position = "p",
                                           domain = "d">>}

// -----

func.func private @element_modes() attributes {
  // expected-error @+1 {{random element modes are malformed}}
  property = #simulation.random_property<field = @f, type = i1, width = 1,
    modeIndex = 0, isSigned = false, isRandC = false, elementModeField = @m>}

// -----

func.func private @edge_without_lsbs() attributes {
  // expected-error @+1 {{edge-sensitive timing path needs source LSBs}}
  rule = #simulation.timing_path_rule<inputs = ["a"], snapshots = ["s"],
    polarity = 0, delays = [1],
    edge = #simulation.timing_path_edge<identifier = 1, pending = "p",
                                        epoch = "e">>}

// -----

func.func private @pulse_limits() attributes {
  // expected-error @+1 {{timing path pulse limits are malformed}}
  rule = #simulation.timing_path_rule<inputs = ["a"], snapshots = ["s"],
    polarity = 0, delays = [1, 2], pulseReject = [1, 1]>}

// -----

func.func private @destination() attributes {
  // expected-error @+1 {{timing path destination is out of bounds}}
  selection = #simulation.timing_path_selection<inputLows = [0],
    inputWidths = [1], outputLow = 3, outputWidth = 2, outputRootWidth = 4>}

// -----

// expected-error @+1 {{attribute 'simulation.steady_unknown' belongs on a storage declaration}}
func.func private @steady_on_function() attributes {
  simulation.steady_unknown = array<i64: 0, 1>}

// -----

simulation.design @steady_unsorted {
  simulation.scope.decl 0
  // expected-error @+1 {{attribute 'simulation.steady_unknown' must hold sorted, disjoint ranges inside the root}}
  simulation.storage.decl 0 in 0 : !simulation.logic<8> design {simulation.steady_unknown = array<i64: 4, 2, 2, 1>}
}

// -----

simulation.design @steady_outside {
  simulation.scope.decl 0
  // expected-error @+1 {{attribute 'simulation.steady_unknown' must hold sorted, disjoint ranges inside the root}}
  simulation.storage.decl 0 in 0 : !simulation.logic<8> design {simulation.steady_unknown = array<i64: 6, 4>}
}

// -----

simulation.design @steady_odd {
  simulation.scope.decl 0
  // expected-error @+1 {{attribute 'simulation.steady_unknown' must hold (offset, width) pairs}}
  simulation.storage.decl 0 in 0 : !simulation.logic<8> design {simulation.steady_unknown = array<i64: 0, 1, 4>}
}
