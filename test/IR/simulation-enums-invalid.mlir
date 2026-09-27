// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  func.func @integer_is_not_an_enum(%ctx: !simulation.context, %seed: i32) {
    // expected-error @+1 {{attribute 'distribution' failed to satisfy constraint}}
    %result:2 = "simulation.random.distribution"(%ctx, %seed, %seed, %seed) {
      distribution = 0 : i32
    } : (!simulation.context, i32, i32, i32) -> (i32, i32)
    return
  }
}

// -----

module {
  func.func @wrong_enum_type(%ctx: !simulation.context, %seed: i32) {
    // expected-error @+1 {{attribute 'distribution' failed to satisfy constraint}}
    %result:2 = "simulation.random.distribution"(%ctx, %seed, %seed, %seed) {
      distribution = #simulation.stochastic_queue_action<initialize>
    } : (!simulation.context, i32, i32, i32) -> (i32, i32)
    return
  }
}

// -----

// expected-error @+2 {{expected valid keyword or string}}
// expected-error @+1 {{failed to parse SimRadixAttr parameter 'value'}}
module attributes {test.radix = #simulation.radix<3>} {}
