// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

// expected-error @+1 {{logic width must be greater than zero}}
func.func private @zero_width(%arg: !simulation.logic<0>)

// -----

func.func private @ref_of_float(%arg: !simulation.ref<f32>)

// -----

// expected-error @+1 {{element type must be a normalized scalar or fixed aggregate, got '!simulation.time'}}
func.func private @net_of_time(%arg: !simulation.net<!simulation.time>)

// -----

// expected-error @+1 {{builtin integer element types must be signless}}
func.func private @driver_of_signed(%arg: !simulation.driver<si8>)

// -----

// expected-error @+1 {{element type must be a normalized scalar or fixed aggregate, got '!simulation.ref<i8>'}}
func.func private @nested_ref(%arg: !simulation.ref<!simulation.ref<i8>>)

// -----

// expected-error @+2 {{string, class, or process key cannot be signed}}
func.func private @signed_string_assoc(
    %arg: !simulation.assoc_array<!simulation.string, i32, true, false>)

// -----

// expected-error @+2 {{string, class, or process key cannot be signed}}
func.func private @signed_class_assoc(
    %arg: !simulation.assoc_array<!simulation.class_handle<@Key>, i32, true, false>)

// -----

// expected-error @+2 {{string, class, or process key cannot be signed}}
func.func private @signed_process_assoc(
    %arg: !simulation.assoc_array<!simulation.process, i32, true, false>)
