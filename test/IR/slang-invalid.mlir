// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  slang.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "bad_width",
    // expected-error @+1 {{integral width must be greater than zero}}
    semantic_type = !slang.integral<0, false, true, 0 : 0, generic>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{requires attribute 'constant_value'}}
  slang.expression.integer_literal attributes {
    node_id = 0 : i64,
    semantic_type = !slang.integral<1, false, true, 0 : 0, logic>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{requires attribute 'operator_kind'}}
  slang.expression.binary_op attributes {
    node_id = 0 : i64,
    semantic_type = !slang.integral<1, false, true, 0 : 0, logic>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{requires attribute 'check_kind'}}
  slang.statement.conditional attributes {
    condition_count = 1 : i64, has_else = false, node_id = 0 : i64
  } {
  }
}

// -----

module {
  // expected-error @+1 {{requires attribute 'sym_name'}}
  slang.type.string_type attributes {
    node_id = 0 : i64, semantic_type = !slang.string
  } {
  }
}

// -----

module {
  // expected-error @+1 {{represents an invalid semantic sentinel}}
  slang.constraint.invalid attributes {node_id = 0 : i64} {
  }
}

// -----

module {
  slang.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "bad_error",
    // expected-error @+1 {{error recovery type cannot appear in valid Slang IR}}
    semantic_type = !slang.error<true>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{attribute 'assignment_kind' failed to satisfy constraint}}
  slang.expression.assignment attributes {
    assignment_kind = 2 : i32, node_id = 0 : i64,
    semantic_type = !slang.integral<1, false, true, 0 : 0, logic>
  } {
  }
}

// -----

module {
  slang.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "bad_range",
    // expected-error @+1 {{declared range width exceeds uint64_t}}
    semantic_type = !slang.integral<1, true, true, 9223372036854775807 : -9223372036854775808, generic>
  } {
  }
}

// -----

module {
  slang.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "bad_element",
    // expected-error @+1 {{packed array element must be packed}}
    semantic_type = !slang.packed_array<3 : 0 x !slang.string>
  } {
  }
}

// -----

module {
  slang.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "bad_index",
    // expected-error @+1 {{wildcard associative index must use !slang.untyped}}
    semantic_type = !slang.associative_array<!slang.string, !slang.real, true>
  } {
  }
}

// -----

module {
  slang.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "bad_aggregate",
    // expected-error @+1 {{only a union can be tagged}}
    semantic_type = !slang.aggregate<"record_t", false, false, true, false, false, false, 0, 0, 0, 0, []>
  } {
  }
}

// -----

module {
  slang.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "missing_field_metadata",
    // expected-error @+1 {{aggregate fields require name, type, ordinal, and packed_offset metadata}}
    semantic_type = !slang.aggregate<"record_t", false, false, false, false, false, false, 0, 0, 0, 0, [{name = "a"}]>
  } {
  }
}

// -----

module {
  slang.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "bad_field_ordinal",
    // expected-error @+1 {{aggregate field ordinals must be dense and ordered}}
    semantic_type = !slang.aggregate<"record_t", false, false, false, false, false, false, 0, 0, 0, 0, [{name = "a", ordinal = 1 : i32, packed_offset = 0 : i64, type = !slang.integral<8, false, false, 7 : 0, bit>}]>
  } {
  }
}

// -----

module {
  slang.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "duplicate_field_name",
    // expected-error @+1 {{aggregate field names must be unique}}
    semantic_type = !slang.aggregate<"record_t", false, false, false, false, false, false, 0, 0, 0, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, type = !slang.integral<8, false, false, 7 : 0, bit>}, {name = "a", ordinal = 1 : i32, packed_offset = 0 : i64, type = !slang.integral<8, false, false, 7 : 0, bit>}]>
  } {
  }
}

// -----

module {
  slang.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "negative_field_offset",
    // expected-error @+1 {{aggregate field has invalid packed offset}}
    semantic_type = !slang.aggregate<"record_t", true, false, false, false, false, false, 8, 8, 8, 0, [{name = "a", ordinal = 0 : i32, packed_offset = -1 : i64, type = !slang.integral<8, false, false, 7 : 0, bit>}]>
  } {
  }
}

// -----

module {
  slang.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "unpacked_field_offset",
    // expected-error @+1 {{aggregate field has invalid packed offset}}
    semantic_type = !slang.aggregate<"record_t", false, false, false, false, false, false, 0, 0, 0, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 1 : i64, type = !slang.integral<8, false, false, 7 : 0, bit>}]>
  } {
  }
}

// -----

module {
  slang.symbol.subroutine attributes {
    node_id = 0 : i64, sym_name = "bad_signature",
    // expected-error @+1 {{subroutine signature must be a function type}}
    semantic_type = !slang.subroutine<!slang.string, false>
  } {
  }
}

// -----

module {
  slang.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "bad_source_range",
    // expected-error @+1 {{source range files must not be empty}}
    semantic_type = !slang.source_range<"", 10, 2, "source.sv", 9, 1, "">
  } {
  }
}

// -----

module {
  // expected-error @+1 {{expects region #0 to have 0 or 1 blocks}}
  slang.statement.block attributes {node_id = 0 : i64} {
  ^first:
  ^second:
  }
}

// -----

module {
  slang.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "bad_declared_range",
    // expected-error @+1 {{declared range width 4 does not match integral width 8}}
    semantic_type = !slang.integral<8, false, true, 3 : 0, generic>
  } {
  }
}
