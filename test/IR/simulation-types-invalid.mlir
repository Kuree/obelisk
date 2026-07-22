// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

// expected-error @+1 {{logic width must be greater than zero}}
func.func private @zero_width(%arg: !obelisk_sim.logic<0>)

// -----

// expected-error @+1 {{element type must be a normalized scalar or fixed aggregate, got 'f32'}}
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
