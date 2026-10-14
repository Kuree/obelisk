// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

// expected-error @+1 {{has undeclared attribute 'runtime.flags'}}
module attributes {runtime.flags = 1 : i32} {}

// -----

// expected-error @+1 {{attribute 'runtime.execution_flags' has the wrong payload kind}}
module attributes {runtime.execution_flags = 1 : i32} {}

// -----

// expected-error @+1 {{attribute 'runtime.state_bits' belongs on a module}}
func.func private @state() attributes {runtime.state_bits = 8 : i64}

// -----

// expected-error @+1 {{attribute 'runtime.sampled_ranges' must hold offset/width pairs}}
module attributes {runtime.sampled_ranges = array<i64: 0>} {}
