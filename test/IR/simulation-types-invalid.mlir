// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

// expected-error @+1 {{logic width must be greater than zero}}
func.func private @zero_width(%arg: !obelisk_sim.logic<0>)

// -----

func.func private @ref_of_float(%arg: !obelisk_sim.ref<f32>)

// -----

// expected-error @+1 {{element type must be a normalized scalar or fixed aggregate, got '!obelisk_sim.time'}}
func.func private @net_of_time(%arg: !obelisk_sim.net<!obelisk_sim.time>)

// -----

// expected-error @+1 {{builtin integer element types must be signless}}
func.func private @driver_of_signed(%arg: !obelisk_sim.driver<si8>)

// -----

// expected-error @+1 {{element type must be a normalized scalar or fixed aggregate, got '!obelisk_sim.ref<i8>'}}
func.func private @nested_ref(%arg: !obelisk_sim.ref<!obelisk_sim.ref<i8>>)

// -----

// expected-error @+2 {{wildcard associative-array indices are not executable}}
func.func private @wildcard_assoc(
    %arg: !obelisk_sim.assoc_array<i32, i32, true, true>)

// -----

// expected-error @+2 {{string or class key cannot be signed}}
func.func private @signed_string_assoc(
    %arg: !obelisk_sim.assoc_array<!obelisk_sim.string, i32, true, false>)

// -----

// expected-error @+2 {{string or class key cannot be signed}}
func.func private @signed_class_assoc(
    %arg: !obelisk_sim.assoc_array<!obelisk_sim.class_handle<@Key>, i32, true, false>)
