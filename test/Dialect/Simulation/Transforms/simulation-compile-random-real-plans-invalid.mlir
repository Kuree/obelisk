// RUN: obelisk-opt %s --obelisk-sim-compile-random-plans --split-input-file --verify-diagnostics
// REQUIRES: compiled-sampler

// Typed IEEE operations require the explicit program flag.
module {
  func.func @missing_real_flag(%ctx: !simulation.context) {
    %zero = arith.constant 0 : i64
    %one = arith.constant 1 : i64
    // expected-error @+1 {{could not compile an exact random sampling plan}}
    %r:3 = simulation.random.solve %ctx from %zero mutable %one constraints %zero limit %one state %zero increment %one captures() "\4F\44\52\31\01\00\18\00\01\00\00\00\03\00\00\00\00\00\00\00\20\00\00\00\22\20\00\00\00\00\00\00\00\00\80\3F\00\00\00\00\2F\20\00\00\00\00\00\00\00\00\00\00\00\00\00\00\20\01\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00" : !simulation.context
    return
  }
}

// -----

// An IEEE marker cannot consume a different-sized payload.
module {
  func.func @wrong_payload_width(%ctx: !simulation.context) {
    %zero = arith.constant 0 : i64
    %one = arith.constant 1 : i64
    // expected-error @+1 {{could not compile an exact random sampling plan}}
    %r:3 = simulation.random.solve %ctx from %zero mutable %one constraints %zero limit %one state %zero increment %one captures() "\4F\44\52\31\01\00\18\00\01\00\00\00\03\00\00\00\00\00\00\00\20\04\00\00\22\40\00\00\00\00\00\00\00\00\00\00\00\00\F0\3F\2F\20\00\00\00\00\00\00\00\00\00\00\00\00\00\00\20\01\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00" : !simulation.context
    return
  }
}

// -----

// Recovering IEEE bits requires an actual typed floating value.
module {
  func.func @raw_integer_bitcast(%ctx: !simulation.context) {
    %zero = arith.constant 0 : i64
    %one = arith.constant 1 : i64
    // expected-error @+1 {{could not compile an exact random sampling plan}}
    %r:3 = simulation.random.solve %ctx from %zero mutable %one constraints %zero limit %one state %zero increment %one captures() "\4F\44\52\31\01\00\18\00\01\00\00\00\03\00\00\00\00\00\00\00\20\04\00\00\22\20\00\00\00\00\00\00\00\00\80\3F\00\00\00\00\35\20\00\00\00\00\00\00\00\00\00\00\00\00\00\00\20\01\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00" : !simulation.context
    return
  }
}

// -----

// Mixed operands require an explicit integral-to-real conversion.
module {
  func.func @mixed_arithmetic(%ctx: !simulation.context) {
    %zero = arith.constant 0 : i64
    %one = arith.constant 1 : i64
    // expected-error @+1 {{could not compile an exact random sampling plan}}
    %r:3 = simulation.random.solve %ctx from %zero mutable %one constraints %zero limit %one state %zero increment %one captures() "\4F\44\52\31\01\00\18\00\01\00\00\00\05\00\00\00\00\00\00\00\20\04\00\00\22\20\00\00\00\00\00\00\00\00\80\3F\00\00\00\00\2F\20\00\00\00\00\00\00\00\00\00\00\00\00\00\00\22\20\00\00\00\00\00\00\01\00\00\00\00\00\00\00\0E\20\00\00\00\00\00\00\00\00\00\00\00\00\00\00\20\01\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00" : !simulation.context
    return
  }
}

// -----

// Integral bitwise operations do not accept IEEE operands.
module {
  func.func @floating_bitwise(%ctx: !simulation.context) {
    %zero = arith.constant 0 : i64
    %one = arith.constant 1 : i64
    // expected-error @+1 {{could not compile an exact random sampling plan}}
    %r:3 = simulation.random.solve %ctx from %zero mutable %one constraints %zero limit %one state %zero increment %one captures() "\4F\44\52\31\01\00\18\00\01\00\00\00\04\00\00\00\00\00\00\00\20\04\00\00\22\20\00\00\00\00\00\00\00\00\80\3F\00\00\00\00\2F\20\00\00\00\00\00\00\00\00\00\00\00\00\00\00\06\20\00\00\00\00\00\00\00\00\00\00\00\00\00\00\20\01\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00" : !simulation.context
    return
  }
}

// -----

// Computed arithmetic cannot claim a platform-independent NaN payload.
module {
  func.func @arithmetic_payload(%ctx: !simulation.context) {
    %zero = arith.constant 0 : i64
    %one = arith.constant 1 : i64
    // expected-error @+1 {{could not compile an exact random sampling plan}}
    %r:3 = simulation.random.solve %ctx from %zero mutable %one constraints %zero limit %one state %zero increment %one captures() "\4F\44\52\31\01\00\18\00\01\00\00\00\07\00\00\00\00\00\00\00\20\04\00\00\22\20\00\00\00\00\00\00\01\00\C0\7F\00\00\00\00\2F\20\00\00\00\00\00\00\00\00\00\00\00\00\00\00\22\20\00\00\00\00\00\00\00\00\80\3F\00\00\00\00\2F\20\00\00\00\00\00\00\00\00\00\00\00\00\00\00\0E\20\00\00\00\00\00\00\00\00\00\00\00\00\00\00\35\20\00\00\00\00\00\00\00\00\00\00\00\00\00\00\20\01\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00" : !simulation.context
    return
  }
}

// -----

// Numeric rank decoders require the complete signed coordinate width.
module {
  func.func @wrong_rank_width(%ctx: !simulation.context) {
    %zero = arith.constant 0 : i64
    %one = arith.constant 1 : i64
    // expected-error @+1 {{could not compile an exact random sampling plan}}
    %r:3 = simulation.random.solve %ctx from %zero mutable %one constraints %zero limit %one state %zero increment %one captures() "\4F\44\52\31\01\00\18\00\01\00\00\00\03\00\00\00\00\00\00\00\20\04\00\00\22\40\00\00\00\00\00\00\01\00\00\00\00\00\00\00\32\40\00\00\00\00\00\00\00\00\00\00\00\00\00\00\20\01\00\00\FF\FF\FF\FF\00\00\00\00\00\00\00\00" : !simulation.context
    return
  }
}
