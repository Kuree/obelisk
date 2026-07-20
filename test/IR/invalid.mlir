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
  // expected-error @+1 {{case comparisons must produce i1}}
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

// -----

module {
  // expected-error @+2 {{failed to parse RefType parameter 'elementType'}}
  // expected-error @+1 {{struct fields must use !hw.struct, got 'i32'}}
  %bad = obelisk.var.alloc : !obelisk.ref<!obelisk.packed_struct<i32>>
}

// -----

module {
  // expected-error @+3 {{failed to parse RefType parameter 'elementType'}}
  // expected-error @+2 {{packed aggregate contains an unpacked field}}
  %bad = obelisk.var.alloc
      : !obelisk.ref<!obelisk.packed_struct<!hw.struct<text: !sim.dstring>>>
}

// -----

module {
  %input = obelisk.var.alloc : !obelisk.ref<!obelisk.logic<8>>
  // expected-error @+1 {{part-select [10:3] exceeds input width 8}}
  %bad = obelisk.ref.extract %input from 3
      : !obelisk.ref<!obelisk.logic<8>> -> !obelisk.ref<!obelisk.logic<8>>
}

// -----

module {
  %input = obelisk.var.alloc : !obelisk.ref<!obelisk.logic<8>>
  %index = arith.constant 0.0 : f32
  // expected-error @+1 {{dynamic index must be a two- or four-state integer}}
  %bad = obelisk.ref.dyn_extract %input from %index
      : (!obelisk.ref<!obelisk.logic<8>>, f32)
        -> !obelisk.ref<!obelisk.logic<1>>
}

// -----

module {
  %input = obelisk.var.alloc : !obelisk.ref<!obelisk.logic<4>>
  // expected-error @+1 {{input widths sum to 8 but result width is 4}}
  %bad = obelisk.ref.concat %input, %input
      : (!obelisk.ref<!obelisk.logic<4>>, !obelisk.ref<!obelisk.logic<4>>)
        -> !obelisk.ref<!obelisk.logic<4>>
}

// -----

module {
  // expected-error @+1 {{opcode assign belongs to the effect family, not value}}
  obelisk.semantic.value assign() : () -> ()
}

// -----

module {
  // expected-error @+1 {{opcode add requires 2 operands, got 0}}
  %bad = obelisk.semantic.value add() : () -> i32
}

// -----

module {
  // expected-error @+1 {{opcode procedure requires source_attrs.kind}}
  obelisk.semantic.region procedure() : () -> () {
    obelisk.semantic.terminator return()
  }
}
