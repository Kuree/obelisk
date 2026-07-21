// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  // expected-error @+2 {{failed to parse RefType parameter 'elementType'}}
  // expected-error @+1 {{logic width must be greater than zero}}
  %bad = obelisk.var.alloc : !obelisk.ref<!obelisk.logic<0>>
}

// -----

module {
  obelisk.sv.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "bad_enum",
    // expected-error @+1 {{enum base must be an integral type}}
    semantic_type = !obelisk.enum<"bad_enum", !obelisk.string>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{requires attribute 'constant_value'}}
  obelisk.sv.expression.integer_literal attributes {
    node_id = 0 : i64,
    semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{requires attribute 'operator_kind'}}
  obelisk.sv.expression.binary_op attributes {
    node_id = 0 : i64,
    semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{represents an invalid semantic sentinel}}
  obelisk.sv.pattern.invalid attributes {node_id = 0 : i64} {
  }
}

// -----

module {
  obelisk.sv.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "bad_error",
    // expected-error @+1 {{error recovery type cannot appear in valid Obelisk IR}}
    semantic_type = !obelisk.error<true>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{attribute 'subroutine_kind' failed to satisfy constraint}}
  obelisk.sv.symbol.subroutine attributes {
    node_id = 0 : i64, sym_name = "bad_subroutine", subroutine_kind = 2 : i32
  } {
  }
}

// -----

module {
  obelisk.sv.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "bad_range",
    // expected-error @+1 {{source range files must not be empty}}
    semantic_type = !obelisk.source_range<"", 10, 2, "source.sv", 9, 1, "">
  } {
  }
}

// -----

module {
  obelisk.sv.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "bad_index",
    // expected-error @+1 {{wildcard associative index must use !obelisk.untyped}}
    semantic_type = !obelisk.assoc<!obelisk.string, !obelisk.real, true>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{expects region #0 to have 0 or 1 blocks}}
  obelisk.sv.statement.block attributes {node_id = 0 : i64} {
  ^first:
  ^second:
  }
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
  // expected-error @+1 {{packed struct must contain at least one field}}
  %bad = obelisk.var.alloc : !obelisk.ref<!obelisk.packed_struct<{}>>
}

// -----

module {
  // expected-error @+3 {{failed to parse RefType parameter 'elementType'}}
  // expected-error @+2 {{packed aggregate field "text" has unpacked type}}
  %bad = obelisk.var.alloc
      : !obelisk.ref<!obelisk.packed_struct<{text = !obelisk.string}>>
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
