// RUN: talos-opt --split-input-file --verify-diagnostics %s

module {
  // expected-error @+2 {{failed to parse RefType parameter 'elementType'}}
  // expected-error @+1 {{logic width must be greater than zero}}
  %bad = talos.var.alloc : !talos.ref<!talos.logic<0>>
}

// -----

module {
  talos.sv.symbol.variable @bad_enum attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{enum base must be an integral type}}
    semantic_type = !talos.enum<"bad_enum", !talos.string>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{setter inventory describes 3 children but body contains 0}}
  talos.sv.expression.structured_assignment_pattern attributes {
    has_default_setter = true, index_setter_count = 1 : i64,
    member_setter_count = 0 : i64, node_id = 0 : i64,
    semantic_type = !talos.integral<32, true, false, 31 : 0, int>,
    type_setter_count = 0 : i64
  } {
  }
}

// -----

module {
  // expected-error @+1 {{iterator loop_dimensions entry #0 requires symbol, nonempty path, and type}}
  talos.sv.statement.foreach_loop attributes {
    loop_dimensions = [{
      has_iterator = true, has_static_range = true,
      iterator_path = "i", left = 3 : i64, right = 0 : i64
    }], node_id = 0 : i64
  } {
    talos.sv.statement.empty attributes {node_id = 1 : i64} {
    }
    talos.sv.statement.empty attributes {node_id = 2 : i64} {
    }
  }
}

// -----

module {
  // expected-error @+1 {{malformed for-loop child inventory}}
  talos.sv.statement.for_loop attributes {
    has_condition = false, initializer_count = 1 : i64,
    node_id = 0 : i64, step_count = 0 : i64
  } {
    talos.sv.statement.empty attributes {node_id = 1 : i64} {
    }
  }
}

// -----

module {
  // expected-error @+1 {{initializer_count must be nonnegative}}
  talos.sv.statement.for_loop attributes {
    has_condition = false, initializer_count = -1 : i64,
    node_id = 0 : i64, step_count = 0 : i64
  } {
    talos.sv.statement.empty attributes {node_id = 1 : i64} {
    }
  }
}

// -----

module {
  // expected-error @+1 {{requires attribute 'constant_value'}}
  talos.sv.expression.integer_literal attributes {
    node_id = 0 : i64,
    semantic_type = !talos.integral<1, false, true, 0 : 0, logic>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{requires attribute 'operator_kind'}}
  talos.sv.expression.binary_op attributes {
    node_id = 0 : i64,
    semantic_type = !talos.integral<1, false, true, 0 : 0, logic>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{represents an invalid semantic sentinel}}
  talos.sv.pattern.invalid attributes {node_id = 0 : i64} {
  }
}

// -----

module {
  // expected-error @+1 {{assertion invocation inventory describes 2 children but body contains 0}}
  talos.sv.expression.assertion_instance attributes {
    argument_count = 1 : i64, argument_formal_paths = ["formal"],
    argument_formal_symbols = [@formal], argument_kinds = array<i64: 0>,
    has_expanded_body = true, is_recursive_property = false,
    local_variable_count = 0 : i64, local_variable_has_initializer = array<i64>,
    local_variable_paths = [], local_variable_symbols = [], node_id = 0 : i64,
    referenced_path = "property", referenced_symbol = @property,
    semantic_type = !talos.property
  } {
  }
}

// -----

module {
  // expected-error @+1 {{condition_pattern_flags must contain one entry per inventory item}}
  talos.sv.statement.conditional attributes {
    check_kind = 0 : i32, condition_count = 1 : i64,
    condition_pattern_flags = array<i64>, has_else = false, node_id = 0 : i64
  } {
  }
}

// -----

module {
  // expected-error @+1 {{every case item must contain at least one label}}
  talos.sv.statement.case attributes {
    check_kind = 0 : i32, condition_kind = 0 : i32, has_default = false,
    item_count = 1 : i64, item_label_counts = array<i64: 0>,
    node_id = 0 : i64
  } {
  }
}

// -----

module {
  // expected-error @+1 {{item_filter_flags entries must be zero or one}}
  talos.sv.statement.pattern_case attributes {
    check_kind = 0 : i32, condition_kind = 0 : i32, has_default = false,
    item_count = 1 : i64, item_filter_flags = array<i64: 2>,
    node_id = 0 : i64
  } {
  }
}

// -----

module {
  // expected-error @+1 {{pattern case cannot use the case-inside matching mode}}
  talos.sv.statement.pattern_case attributes {
    check_kind = 0 : i32, condition_kind = 3 : i32, has_default = false,
    item_count = 0 : i64, item_filter_flags = array<i64>, node_id = 0 : i64
  } {
  }
}

// -----

module {
  // expected-error @+1 {{inside set must contain at least one item}}
  talos.sv.expression.inside attributes {
    item_count = 0 : i64, node_id = 0 : i64,
    semantic_type = !talos.integral<1, false, true, 0 : 0, logic>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{inside item inventory overflows}}
  talos.sv.expression.inside attributes {
    item_count = -1 : i64, node_id = 0 : i64,
    semantic_type = !talos.integral<1, false, true, 0 : 0, logic>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{value range must contain exactly two endpoints}}
  talos.sv.expression.value_range attributes {
    node_id = 0 : i64, range_kind = 0 : i32, semantic_type = !talos.void
  } {
  }
}

// -----

module {
  // expected-error @+1 {{structure pattern field ordinals must be nonnegative and unique}}
  talos.sv.pattern.structure attributes {
    field_ordinals = array<i64: 0, 0>, node_id = 0 : i64
  } {
    talos.sv.pattern.wildcard attributes {node_id = 1 : i64} {
    }
    talos.sv.pattern.wildcard attributes {node_id = 2 : i64} {
    }
  }
}

// -----

module {
  talos.sv.symbol.root @root attributes {node_id = 0 : i64} {
    // expected-error @+1 {{cannot resolve "referenced_symbol" @missing}}
    talos.sv.pattern.variable attributes {
      node_id = 1 : i64, referenced_path = "missing", referenced_symbol = @missing
    } {
    }
  }
}

// -----

module {
  talos.sv.symbol.root @root attributes {node_id = 0 : i64} {
    talos.sv.symbol.variable @ordinary attributes {
      lifetime = 0 : i32, node_id = 1 : i64, rand_mode = 0 : i32,
      semantic_type = !talos.integral<4, false, true, 3 : 0, logic>
    } {
    }
    // expected-error @+1 {{referenced pattern variable does not resolve to a pattern binding}}
    talos.sv.pattern.variable attributes {
      node_id = 2 : i64, referenced_path = "ordinary",
      referenced_symbol = @ordinary
    } {
    }
  }
}

// -----

module {
  talos.sv.symbol.root @root attributes {node_id = 0 : i64} {
    // expected-error @+1 {{cannot resolve "referenced_symbol" @missing}}
    talos.sv.pattern.tagged attributes {
      field_ordinal = 0 : i64, node_id = 1 : i64, packed_offset = 0 : i64,
      referenced_path = "missing.member", referenced_symbol = @missing
    } {
    }
  }
}

// -----

module {
  talos.sv.symbol.root @root attributes {node_id = 0 : i64} {
    talos.sv.symbol.field @member attributes {
      bit_offset = 0 : i64, field_index = 1 : i64, lifetime = 0 : i32,
      node_id = 1 : i64, rand_mode = 0 : i32,
      semantic_type = !talos.integral<4, false, true, 3 : 0, logic>
    } {
    }
    // expected-error @+1 {{tagged pattern field metadata does not match its referenced member}}
    talos.sv.pattern.tagged attributes {
      field_ordinal = 0 : i64, node_id = 2 : i64, packed_offset = 0 : i64,
      referenced_path = "member", referenced_symbol = @member
    } {
    }
  }
}

// -----

module {
  talos.sv.symbol.variable @bad_error attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{error recovery type cannot appear in valid Talos IR}}
    semantic_type = !talos.error<true>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{attribute 'subroutine_kind' failed to satisfy constraint}}
  talos.sv.symbol.subroutine @bad_subroutine attributes {
    node_id = 0 : i64, subroutine_kind = 2 : i32
  } {
  }
}

// -----

module {
  talos.sv.symbol.variable @bad_range attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{source range files must not be empty}}
    semantic_type = !talos.source_range<"", 10, 2, "source.sv", 9, 1, "">
  } {
  }
}

// -----

module {
  talos.sv.symbol.variable @bad_index attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{wildcard associative index must use !talos.untyped}}
    semantic_type = !talos.assoc<!talos.string, !talos.real, true>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{expects region #0 to have 0 or 1 blocks}}
  talos.sv.statement.block attributes {node_id = 0 : i64} {
  ^first:
  ^second:
  }
}

// -----

module {
  // expected-error @+1 {{unknown attribute width must match result width 8}}
  %bad = talos.logic.constant 0 : i8, 0 : i4 : !talos.logic<8>
}

// -----

module {
  %lhs = talos.logic.constant 0 : i8, 0 : i8 : !talos.logic<8>
  %rhs = talos.logic.constant 1 : i8, 0 : i8 : !talos.logic<8>
  // expected-error @+1 {{case comparisons must produce i1}}
  %bad = talos.logic.compare case_eq %lhs, %rhs
      : (!talos.logic<8>, !talos.logic<8>) -> !talos.logic<8>
}

// -----

module {
  %logic = talos.logic.constant 0 : i8, 0 : i8 : !talos.logic<8>
  %ref = talos.var.alloc = %logic : !talos.logic<8>
      : !talos.ref<!talos.logic<8>>
  %integer = arith.constant 0 : i32
  // expected-error @+1 {{reference element type must match value type}}
  talos.store %integer to %ref
      : i32, !talos.ref<!talos.logic<8>>
}

// -----

module {
  // expected-error @+2 {{failed to parse RefType parameter 'elementType'}}
  // expected-error @+1 {{packed struct must contain at least one field}}
  %bad = talos.var.alloc : !talos.ref<!talos.packed_struct<{}>>
}

// -----

module {
  // expected-error @+3 {{failed to parse RefType parameter 'elementType'}}
  // expected-error @+2 {{packed aggregate field "text" has unpacked type}}
  %bad = talos.var.alloc
      : !talos.ref<!talos.packed_struct<{text = !talos.string}>>
}

// -----

module {
  %input = talos.var.alloc : !talos.ref<!talos.logic<8>>
  // expected-error @+1 {{part-select [10:3] exceeds input width 8}}
  %bad = talos.ref.extract %input from 3
      : !talos.ref<!talos.logic<8>> -> !talos.ref<!talos.logic<8>>
}

// -----

module {
  %input = talos.var.alloc : !talos.ref<!talos.logic<8>>
  %index = arith.constant 0.0 : f32
  // expected-error @+1 {{dynamic index must be a two- or four-state integer}}
  %bad = talos.ref.dyn_extract %input from %index
      : (!talos.ref<!talos.logic<8>>, f32)
        -> !talos.ref<!talos.logic<1>>
}

// -----

module {
  %input = talos.var.alloc : !talos.ref<!talos.logic<4>>
  // expected-error @+1 {{input widths sum to 8 but result width is 4}}
  %bad = talos.ref.concat %input, %input
      : (!talos.ref<!talos.logic<4>>, !talos.ref<!talos.logic<4>>)
        -> !talos.ref<!talos.logic<4>>
}

// -----

module {
  %a = talos.logic.constant 0 : i4, 0 : i4 : !talos.logic<4>
  // expected-error @+1 {{input widths sum to 8 but result width is 4}}
  %bad = talos.logic.concat %a, %a
      : (!talos.logic<4>, !talos.logic<4>) -> !talos.logic<4>
}

// -----

module {
  %a = talos.logic.constant 0 : i8, 0 : i8 : !talos.logic<8>
  // expected-error @+1 {{kind must be shift_left, shift_right, or ashift_right}}
  %bad = talos.logic.shift add %a by %a
      : (!talos.logic<8>, !talos.logic<8>) -> !talos.logic<8>
}

// -----

module {
  %a = talos.logic.constant 0 : i3, 0 : i3 : !talos.logic<3>
  // expected-error @+1 {{result width 8 is not a multiple of input width 3}}
  %bad = talos.logic.replicate %a : !talos.logic<3> -> !talos.logic<8>
}

// -----

module {
  %a = talos.logic.constant 0 : i8, 0 : i8 : !talos.logic<8>
  %r = talos.logic.constant 0 : i4, 0 : i4 : !talos.logic<4>
  // expected-error @+1 {{replacement at bit 6 exceeds input width 8}}
  %bad = talos.logic.insert %r into %a at 6
      : (!talos.logic<8>, !talos.logic<4>) -> !talos.logic<8>
}

// -----

module {
  %i = arith.constant 0 : i8
  // expected-error @+1 {{input width 8 does not match result width 4}}
  %bad = talos.logic.from_bits %i : i8 -> !talos.logic<4>
}

// -----

module {
  %a = talos.logic.constant 0 : i8, 0 : i8 : !talos.logic<8>
  // expected-error @+1 {{input width 8 does not match result width 4}}
  %bad = talos.logic.to_bits %a : !talos.logic<8> -> i4
}

// -----

module {
  %n = talos.net.alloc wire : !talos.net<!talos.logic<8>>
  // expected-error @+1 {{part-select [10:3] exceeds input width 8}}
  %bad = talos.net.extract %n from 3
      : !talos.net<!talos.logic<8>> -> !talos.net<!talos.logic<8>>
}

// -----

module {
  %n = talos.net.alloc wire : !talos.net<!talos.logic<4>>
  // expected-error @+1 {{input widths sum to 8 but result width is 4}}
  %bad = talos.net.concat %n, %n
      : (!talos.net<!talos.logic<4>>, !talos.net<!talos.logic<4>>)
        -> !talos.net<!talos.logic<4>>
}

// -----

module {
  %n = talos.net.alloc wire : !talos.net<!talos.logic<8>>
  %index = arith.constant 0.0 : f32
  // expected-error @+1 {{dynamic index must be a two- or four-state integer}}
  %bad = talos.net.dyn_extract %n from %index
      : (!talos.net<!talos.logic<8>>, f32)
        -> !talos.net<!talos.logic<1>>
}

// -----

module {
  talos.sv.symbol.variable @bad_task attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{task signature must not have a result}}
    semantic_type = !talos.subroutine<() -> !talos.string, true>
  } {
  }
}

// -----

module {
  talos.sv.symbol.variable @bad_function attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{function signature must have exactly one result}}
    semantic_type = !talos.subroutine<() -> (), false>
  } {
  }
}

// -----

module {
  talos.sv.symbol.variable @bad_integral_range attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{declared range width 4 does not match integral width 8}}
    semantic_type = !talos.integral<8, false, true, 3 : 0, generic>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{only a packed union can be soft}}
  talos.sv.symbol.variable @bad_soft attributes {node_id = 0 : i64, semantic_type = !talos.source_aggregate<"rec", false, false, false, false, false, true, 0, 0, 0, 0, []>} {
  }
}

// -----

module {
  // expected-error @+1 {{aggregate field ordinals must be dense and ordered}}
  talos.sv.symbol.variable @bad_field_ordinal attributes {node_id = 0 : i64, semantic_type = !talos.source_aggregate<"rec", false, false, false, false, false, false, 0, 0, 0, 0, [{name = "a", ordinal = 1 : i32, packed_offset = 0 : i64, type = !talos.integral<8, false, false, 7 : 0, bit>}]>} {
  }
}

// -----

module {
  // expected-error @+1 {{aggregate field has invalid packed offset}}
  talos.sv.symbol.variable @bad_field_offset attributes {node_id = 0 : i64, semantic_type = !talos.source_aggregate<"rec", false, false, false, false, false, false, 0, 0, 0, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 1 : i64, type = !talos.integral<8, false, false, 7 : 0, bit>}]>} {
  }
}

// -----

module {
  // expected-error @+1 {{aggregate field has invalid randomization mode}}
  talos.sv.symbol.variable @bad_field_rand_mode attributes {node_id = 0 : i64, semantic_type = !talos.source_aggregate<"rec", false, false, false, false, false, false, 0, 0, 0, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 3 : i32, type = !talos.integral<8, false, false, 7 : 0, bit>}]>} {
  }
}

// -----

module {
  // expected-error @+2 {{failed to parse RefType parameter 'elementType'}}
  // expected-error @+1 {{packed union fields must have equal widths}}
  %bad = talos.var.alloc : !talos.ref<!talos.packed_union<{a = !talos.logic<4>, b = !talos.logic<8>}>>
}

// -----

module {
  // expected-error @+2 {{failed to parse RefType parameter 'elementType'}}
  // expected-error @+1 {{packed array element must be packed}}
  %bad = talos.var.alloc : !talos.ref<!talos.ranged_packed_array<3 : 0 x !talos.string>>
}

// -----

module {
  // expected-error @+1 {{condition_pattern_flags must contain one entry per inventory item}}
  talos.sv.expression.conditional_op attributes {
    condition_count = 2 : i64,
    condition_pattern_flags = array<i64: 0>, node_id = 0 : i64,
    semantic_type = !talos.integral<1, false, true, 0 : 0, logic>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{randcase must contain at least one item}}
  talos.sv.statement.rand_case attributes {item_count = 0 : i64, node_id = 0 : i64} {
  }
}

// -----

module {
  // Each item owns one weight expression and one statement.
  // expected-error @+1 {{malformed randcase item inventory}}
  talos.sv.statement.rand_case attributes {item_count = 1 : i64, node_id = 0 : i64} {
    talos.sv.expression.integer_literal attributes {
      constant_value = "1", node_id = 1 : i64,
      semantic_type = !talos.integral<32, true, false, 31 : 0, int>
    } {
    }
  }
}
