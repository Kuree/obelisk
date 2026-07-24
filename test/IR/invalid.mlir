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
  // expected-error @+1 {{iterator loop_dimensions entry #0 requires symbol, nonempty path, and type}}
  obelisk.sv.statement.foreach_loop attributes {
    loop_dimensions = [{
      has_iterator = true, has_static_range = true,
      iterator_path = "i", left = 3 : i64, right = 0 : i64
    }], node_id = 0 : i64
  } {
    obelisk.sv.statement.empty attributes {node_id = 1 : i64} {
    }
    obelisk.sv.statement.empty attributes {node_id = 2 : i64} {
    }
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
  // expected-error @+1 {{condition_pattern_flags must contain one entry per inventory item}}
  obelisk.sv.statement.conditional attributes {
    check_kind = 0 : i32, condition_count = 1 : i64,
    condition_pattern_flags = array<i64>, has_else = false, node_id = 0 : i64
  } {
  }
}

// -----

module {
  // expected-error @+1 {{every case item must contain at least one label}}
  obelisk.sv.statement.case attributes {
    check_kind = 0 : i32, condition_kind = 0 : i32, has_default = false,
    item_count = 1 : i64, item_label_counts = array<i64: 0>,
    node_id = 0 : i64
  } {
  }
}

// -----

module {
  // expected-error @+1 {{item_filter_flags entries must be zero or one}}
  obelisk.sv.statement.pattern_case attributes {
    check_kind = 0 : i32, condition_kind = 0 : i32, has_default = false,
    item_count = 1 : i64, item_filter_flags = array<i64: 2>,
    node_id = 0 : i64
  } {
  }
}

// -----

module {
  // expected-error @+1 {{pattern case cannot use the case-inside matching mode}}
  obelisk.sv.statement.pattern_case attributes {
    check_kind = 0 : i32, condition_kind = 3 : i32, has_default = false,
    item_count = 0 : i64, item_filter_flags = array<i64>, node_id = 0 : i64
  } {
  }
}

// -----

module {
  // expected-error @+1 {{inside set must contain at least one item}}
  obelisk.sv.expression.inside attributes {
    item_count = 0 : i64, node_id = 0 : i64,
    semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{inside item inventory overflows}}
  obelisk.sv.expression.inside attributes {
    item_count = -1 : i64, node_id = 0 : i64,
    semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{value range must contain exactly two endpoints}}
  obelisk.sv.expression.value_range attributes {
    node_id = 0 : i64, range_kind = 0 : i32, semantic_type = !obelisk.void
  } {
  }
}

// -----

module {
  // expected-error @+1 {{structure pattern field ordinals must be nonnegative and unique}}
  obelisk.sv.pattern.structure attributes {
    field_ordinals = array<i64: 0, 0>, node_id = 0 : i64
  } {
    obelisk.sv.pattern.wildcard attributes {node_id = 1 : i64} {
    }
    obelisk.sv.pattern.wildcard attributes {node_id = 2 : i64} {
    }
  }
}

// -----

module {
  obelisk.sv.symbol.root attributes {node_id = 0 : i64, sym_name = "root"} {
    // expected-error @+1 {{cannot resolve "referenced_symbol" @missing}}
    obelisk.sv.pattern.variable attributes {
      node_id = 1 : i64, referenced_path = "missing", referenced_symbol = @missing
    } {
    }
  }
}

// -----

module {
  obelisk.sv.symbol.root attributes {node_id = 0 : i64, sym_name = "root"} {
    obelisk.sv.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 1 : i64, rand_mode = 0 : i32,
      semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
      sym_name = "ordinary"
    } {
    }
    // expected-error @+1 {{referenced pattern variable does not resolve to a pattern binding}}
    obelisk.sv.pattern.variable attributes {
      node_id = 2 : i64, referenced_path = "ordinary",
      referenced_symbol = @ordinary
    } {
    }
  }
}

// -----

module {
  obelisk.sv.symbol.root attributes {node_id = 0 : i64, sym_name = "root"} {
    // expected-error @+1 {{cannot resolve "referenced_symbol" @missing}}
    obelisk.sv.pattern.tagged attributes {
      field_ordinal = 0 : i64, node_id = 1 : i64, packed_offset = 0 : i64,
      referenced_path = "missing.member", referenced_symbol = @missing
    } {
    }
  }
}

// -----

module {
  obelisk.sv.symbol.root attributes {node_id = 0 : i64, sym_name = "root"} {
    obelisk.sv.symbol.field attributes {
      bit_offset = 0 : i64, field_index = 1 : i64, lifetime = 0 : i32,
      node_id = 1 : i64, rand_mode = 0 : i32,
      semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>,
      sym_name = "member"
    } {
    }
    // expected-error @+1 {{tagged pattern field metadata does not match its referenced member}}
    obelisk.sv.pattern.tagged attributes {
      field_ordinal = 0 : i64, node_id = 2 : i64, packed_offset = 0 : i64,
      referenced_path = "member", referenced_symbol = @member
    } {
    }
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

// -----

module {
  %a = obelisk.logic.constant 0 : i4, 0 : i4 : !obelisk.logic<4>
  // expected-error @+1 {{input widths sum to 8 but result width is 4}}
  %bad = obelisk.logic.concat %a, %a
      : (!obelisk.logic<4>, !obelisk.logic<4>) -> !obelisk.logic<4>
}

// -----

module {
  %a = obelisk.logic.constant 0 : i8, 0 : i8 : !obelisk.logic<8>
  // expected-error @+1 {{kind must be shift_left, shift_right, or ashift_right}}
  %bad = obelisk.logic.shift add %a by %a
      : (!obelisk.logic<8>, !obelisk.logic<8>) -> !obelisk.logic<8>
}

// -----

module {
  %a = obelisk.logic.constant 0 : i3, 0 : i3 : !obelisk.logic<3>
  // expected-error @+1 {{result width 8 is not a multiple of input width 3}}
  %bad = obelisk.logic.replicate %a : !obelisk.logic<3> -> !obelisk.logic<8>
}

// -----

module {
  %a = obelisk.logic.constant 0 : i8, 0 : i8 : !obelisk.logic<8>
  %r = obelisk.logic.constant 0 : i4, 0 : i4 : !obelisk.logic<4>
  // expected-error @+1 {{replacement at bit 6 exceeds input width 8}}
  %bad = obelisk.logic.insert %r into %a at 6
      : (!obelisk.logic<8>, !obelisk.logic<4>) -> !obelisk.logic<8>
}

// -----

module {
  %i = arith.constant 0 : i8
  // expected-error @+1 {{input width 8 does not match result width 4}}
  %bad = obelisk.logic.from_bits %i : i8 -> !obelisk.logic<4>
}

// -----

module {
  %a = obelisk.logic.constant 0 : i8, 0 : i8 : !obelisk.logic<8>
  // expected-error @+1 {{input width 8 does not match result width 4}}
  %bad = obelisk.logic.to_bits %a : !obelisk.logic<8> -> i4
}

// -----

module {
  %n = obelisk.net.alloc wire : !obelisk.net<!obelisk.logic<8>>
  // expected-error @+1 {{part-select [10:3] exceeds input width 8}}
  %bad = obelisk.net.extract %n from 3
      : !obelisk.net<!obelisk.logic<8>> -> !obelisk.net<!obelisk.logic<8>>
}

// -----

module {
  %n = obelisk.net.alloc wire : !obelisk.net<!obelisk.logic<4>>
  // expected-error @+1 {{input widths sum to 8 but result width is 4}}
  %bad = obelisk.net.concat %n, %n
      : (!obelisk.net<!obelisk.logic<4>>, !obelisk.net<!obelisk.logic<4>>)
        -> !obelisk.net<!obelisk.logic<4>>
}

// -----

module {
  %n = obelisk.net.alloc wire : !obelisk.net<!obelisk.logic<8>>
  %index = arith.constant 0.0 : f32
  // expected-error @+1 {{dynamic index must be a two- or four-state integer}}
  %bad = obelisk.net.dyn_extract %n from %index
      : (!obelisk.net<!obelisk.logic<8>>, f32)
        -> !obelisk.net<!obelisk.logic<1>>
}

// -----

module {
  obelisk.sv.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "bad_task",
    // expected-error @+1 {{task signature must not have a result}}
    semantic_type = !obelisk.subroutine<() -> !obelisk.string, true>
  } {
  }
}

// -----

module {
  obelisk.sv.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "bad_function",
    // expected-error @+1 {{function signature must have exactly one result}}
    semantic_type = !obelisk.subroutine<() -> (), false>
  } {
  }
}

// -----

module {
  obelisk.sv.symbol.variable attributes {
    node_id = 0 : i64, sym_name = "bad_integral_range",
    // expected-error @+1 {{declared range width 4 does not match integral width 8}}
    semantic_type = !obelisk.integral<8, false, true, 3 : 0, generic>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{only a packed union can be soft}}
  obelisk.sv.symbol.variable attributes {node_id = 0 : i64, sym_name = "bad_soft", semantic_type = !obelisk.source_aggregate<"rec", false, false, false, false, false, true, 0, 0, 0, 0, []>} {
  }
}

// -----

module {
  // expected-error @+1 {{aggregate field ordinals must be dense and ordered}}
  obelisk.sv.symbol.variable attributes {node_id = 0 : i64, sym_name = "bad_field_ordinal", semantic_type = !obelisk.source_aggregate<"rec", false, false, false, false, false, false, 0, 0, 0, 0, [{name = "a", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<8, false, false, 7 : 0, bit>}]>} {
  }
}

// -----

module {
  // expected-error @+1 {{aggregate field has invalid packed offset}}
  obelisk.sv.symbol.variable attributes {node_id = 0 : i64, sym_name = "bad_field_offset", semantic_type = !obelisk.source_aggregate<"rec", false, false, false, false, false, false, 0, 0, 0, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 1 : i64, type = !obelisk.integral<8, false, false, 7 : 0, bit>}]>} {
  }
}

// -----

module {
  // expected-error @+2 {{failed to parse RefType parameter 'elementType'}}
  // expected-error @+1 {{packed union fields must have equal widths}}
  %bad = obelisk.var.alloc : !obelisk.ref<!obelisk.packed_union<{a = !obelisk.logic<4>, b = !obelisk.logic<8>}>>
}

// -----

module {
  // expected-error @+2 {{failed to parse RefType parameter 'elementType'}}
  // expected-error @+1 {{packed array element must be packed}}
  %bad = obelisk.var.alloc : !obelisk.ref<!obelisk.ranged_packed_array<3 : 0 x !obelisk.string>>
}
