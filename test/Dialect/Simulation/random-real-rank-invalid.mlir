// RUN: talos-opt %s --split-input-file --verify-diagnostics

module {
  func.func @half(%value: f16) {
    // expected-error @+1 {{requires IEEE binary32 or binary64}}
    %rank, %finite = simulation.random.real_to_rank %value : f16 -> i279
    return
  }
}

// -----

module {
  func.func @double_width(%value: f64) {
    // expected-error @+1 {{requires a 2100-bit signed numeric coordinate}}
    %rank, %finite = simulation.random.real_to_rank %value : f64 -> i279
    return
  }
}

// -----

module {
  func.func @single_width(%value: f32) {
    // expected-error @+1 {{requires a 279-bit signed numeric coordinate}}
    %rank, %finite = simulation.random.real_to_rank %value : f32 -> i2100
    return
  }
}

// -----

module {
  func.func @wrong_decode(%rank: i279) {
    // expected-error @+1 {{requires a 2100-bit signed numeric coordinate}}
    %value = simulation.random.real_from_rank %rank : i279 -> f64
    return
  }
}

// -----

module {
  func.func @bfloat(%rank: i279) {
    // expected-error @+1 {{requires IEEE binary32 or binary64}}
    %value = simulation.random.real_from_rank %rank : i279 -> bf16
    return
  }
}
