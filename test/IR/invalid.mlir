// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  // expected-error @+2 {{failed to parse RefType parameter 'elementType'}}
  // expected-error @+1 {{logic width must be greater than zero}}
  %bad = obelisk.var.alloc : !obelisk.ref<!obelisk.logic<0>>
}

// -----

module {
  // expected-error @+1 {{unknown attribute width must match result width 8}}
  %bad = obelisk.logic.constant 0 : i8, 0 : i4 : !obelisk.logic<8>
}

// -----

module {
  %lhs = obelisk.logic.constant 0 : i8, 0 : i8 : !obelisk.logic<8>
  %rhs = obelisk.logic.constant 1 : i8, 0 : i8 : !obelisk.logic<8>
  // expected-error @+1 {{must be one-bit exact four-state logic}}
  %bad = obelisk.logic.compare case_eq %lhs, %rhs
      : (!obelisk.logic<8>, !obelisk.logic<8>) -> !obelisk.logic<8>
}

// -----

module {
  %logic = obelisk.logic.constant 0 : i8, 0 : i8 : !obelisk.logic<8>
  %ref = obelisk.var.alloc = %logic : !obelisk.logic<8>
      : !obelisk.ref<!obelisk.logic<8>>
  %integer = arith.constant 0 : i32
  // expected-error @+1 {{reference element type must match value type}}
  obelisk.store %integer to %ref
      : i32, !obelisk.ref<!obelisk.logic<8>>
}
