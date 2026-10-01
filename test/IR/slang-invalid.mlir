// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  // expected-error @+1 {{is_from_bind must be true when present}}
  slang.symbol.instance @child attributes {
    is_from_bind = false, node_id = 0 : i64, selected_cell = "work.child"
  } {
  }
}

// -----

module {
  // expected-error @+1 {{requires selected_cell exactly for bind or configuration provenance}}
  slang.symbol.instance @target attributes {
    is_bind_target = true, node_id = 0 : i64
  } {
  }
}

// -----

module {
  // expected-error @+1 {{bind provenance flags must be mutually exclusive}}
  slang.symbol.instance @child attributes {
    is_below_bind = true, is_from_bind = true, node_id = 0 : i64,
    selected_cell = "work.child"
  } {
  }
}

// -----

module {
  // expected-error @+1 {{bind provenance flags must be mutually exclusive}}
  slang.symbol.instance @child attributes {
    is_bind_target = true, is_from_bind = true, node_id = 0 : i64,
    selected_cell = "work.child"
  } {
  }
}

// -----

module {
  // expected-error @+1 {{bind provenance flags must be mutually exclusive}}
  slang.symbol.instance @child attributes {
    is_below_bind = true, is_bind_target = true, node_id = 0 : i64,
    selected_cell = "work.child"
  } {
  }
}

// -----

module {
  // expected-error @+1 {{requires selected_cell exactly for bind or configuration provenance}}
  slang.symbol.instance @child attributes {
    node_id = 0 : i64, selected_cell = "work.child"
  } {
  }
}

// -----

module {
  // expected-error @+1 {{selected_cell must be nonempty}}
  slang.symbol.instance @child attributes {
    is_from_bind = true, node_id = 0 : i64, selected_cell = ""
  } {
  }
}

// -----

module {
  // expected-error @+1 {{requires configuration, configuration_root, and configuration_liblist together}}
  slang.symbol.instance @child attributes {
    configuration = "work.cfg", node_id = 0 : i64,
    selected_cell = "work.child"
  } {
  }
}

// -----

module {
  // expected-error @+1 {{configuration must be nonempty}}
  slang.symbol.instance @child attributes {
    configuration = "", configuration_liblist = ["work"],
    configuration_root = "top", node_id = 0 : i64,
    selected_cell = "work.child"
  } {
  }
}

// -----

module {
  // expected-error @+1 {{configuration_root must be nonempty}}
  slang.symbol.instance @child attributes {
    configuration = "work.cfg", configuration_liblist = ["work"],
    configuration_root = "", node_id = 0 : i64,
    selected_cell = "work.child"
  } {
  }
}

// -----

module {
  // expected-error @+1 {{configuration_liblist entries must be nonempty}}
  slang.symbol.instance @child attributes {
    configuration = "work.cfg", configuration_liblist = [""],
    configuration_root = "top", node_id = 0 : i64,
    selected_cell = "work.child"
  } {
  }
}

// -----

module {
  // expected-error @+1 {{requires configuration rule kind and source range together}}
  slang.symbol.instance @child attributes {
    configuration = "work.cfg", configuration_liblist = ["work"],
    configuration_root = "top", configuration_rule_kind = "cell",
    node_id = 0 : i64, selected_cell = "work.child"
  } {
  }
}

// -----

module {
  // expected-error @+1 {{requires configuration rule kind and source range together}}
  slang.symbol.instance @child attributes {
    configuration = "work.cfg", configuration_liblist = ["work"],
    configuration_root = "top",
    configuration_rule_source_range = !slang.source_range<"config.sv", 1, 1, "config.sv", 1, 20, "">,
    node_id = 0 : i64, selected_cell = "work.child"
  } {
  }
}

// -----

module {
  // expected-error @+1 {{configuration rule metadata requires a configuration}}
  slang.symbol.instance @child attributes {
    configuration_rule_kind = "cell",
    configuration_rule_source_range = !slang.source_range<"config.sv", 1, 1, "config.sv", 1, 20, "">,
    is_from_bind = true, node_id = 0 : i64,
    selected_cell = "work.child"
  } {
  }
}

// -----

module {
  // expected-error @+1 {{configuration rule source range must be !slang.source_range}}
  slang.symbol.instance @child attributes {
    configuration = "work.cfg", configuration_liblist = ["work"],
    configuration_root = "top", configuration_rule_kind = "cell",
    configuration_rule_source_range = i32, node_id = 0 : i64,
    selected_cell = "work.child"
  } {
  }
}

// -----

module {
  // expected-error @+1 {{has invalid configuration rule kind 'other'}}
  slang.symbol.instance @child attributes {
    configuration = "work.cfg", configuration_liblist = ["work"],
    configuration_root = "top", configuration_rule_kind = "other",
    configuration_rule_source_range = !slang.source_range<"config.sv", 1, 1, "config.sv", 1, 20, "">,
    node_id = 0 : i64, selected_cell = "work.child"
  } {
  }
}

// -----

module {
  // expected-error @+1 {{bind provenance flags must be mutually exclusive}}
  slang.symbol.checker_instance @instance attributes {
    connection_actual_kinds = array<i64>, connection_attribute_counts = array<i64>,
    connection_count = 0 : i64, connection_formal_paths = [],
    connection_formal_symbols = [], connection_has_actual = array<i64>,
    connection_has_output_initial = array<i64>, is_below_bind = true,
    is_from_bind = true, is_procedural = false, node_id = 0 : i64,
    referenced_checker_path = "checker", referenced_checker_symbol = @checker,
    selected_cell = "work.checker"
  } {
  }
}

// -----

module {
  // expected-error @+1 {{bind provenance flags must be mutually exclusive}}
  slang.symbol.checker_instance @instance attributes {
    connection_actual_kinds = array<i64>, connection_attribute_counts = array<i64>,
    connection_count = 0 : i64, connection_formal_paths = [],
    connection_formal_symbols = [], connection_has_actual = array<i64>,
    connection_has_output_initial = array<i64>, is_bind_target = true,
    is_from_bind = true, is_procedural = false, node_id = 0 : i64,
    referenced_checker_path = "checker", referenced_checker_symbol = @checker,
    selected_cell = "work.checker"
  } {
  }
}

// -----

module {
  // expected-error @+1 {{bind provenance flags must be mutually exclusive}}
  slang.symbol.checker_instance @instance attributes {
    connection_actual_kinds = array<i64>, connection_attribute_counts = array<i64>,
    connection_count = 0 : i64, connection_formal_paths = [],
    connection_formal_symbols = [], connection_has_actual = array<i64>,
    connection_has_output_initial = array<i64>, is_below_bind = true,
    is_bind_target = true, is_procedural = false, node_id = 0 : i64,
    referenced_checker_path = "checker", referenced_checker_symbol = @checker,
    selected_cell = "work.checker"
  } {
  }
}

// -----

module {
  // expected-error @+1 {{requires selected_cell exactly for bind or configuration provenance}}
  slang.symbol.checker_instance @instance attributes {
    connection_actual_kinds = array<i64>, connection_attribute_counts = array<i64>,
    connection_count = 0 : i64, connection_formal_paths = [],
    connection_formal_symbols = [], connection_has_actual = array<i64>,
    connection_has_output_initial = array<i64>, is_procedural = false,
    node_id = 0 : i64, referenced_checker_path = "checker",
    referenced_checker_symbol = @checker, selected_cell = "work.checker"
  } {
  }
}

// -----

module {
  // expected-error @+1 {{selected_cell must be nonempty}}
  slang.symbol.checker_instance @instance attributes {
    connection_actual_kinds = array<i64>, connection_attribute_counts = array<i64>,
    connection_count = 0 : i64, connection_formal_paths = [],
    connection_formal_symbols = [], connection_has_actual = array<i64>,
    connection_has_output_initial = array<i64>, is_from_bind = true,
    is_procedural = false, node_id = 0 : i64,
    referenced_checker_path = "checker", referenced_checker_symbol = @checker,
    selected_cell = ""
  } {
  }
}

// -----

module {
  slang.symbol.variable @bad_width attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{integral width must be greater than zero}}
    semantic_type = !slang.integral<0, false, true, 0 : 0, generic>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{requires connection metadata arrays to match connection_count}}
  slang.symbol.checker_instance @instance attributes {
    connection_actual_kinds = array<i64: 0>,
    connection_attribute_counts = array<i64: 0>, connection_count = 2 : i64,
    connection_formal_paths = ["formal"],
    connection_formal_symbols = [@formal],
    connection_has_actual = array<i64: 1>,
    connection_has_output_initial = array<i64: 0>, is_procedural = false,
    node_id = 0 : i64, referenced_checker_path = "checker",
    referenced_checker_symbol = @checker
  } {
  }
}

// -----

module {
  // expected-error @+1 {{requires instance metadata arrays to match instance_count}}
  slang.statement.procedural_checker attributes {
    instance_count = 1 : i64, instance_paths = [], instance_symbols = [],
    node_id = 0 : i64
  } {
  }
}

// -----

module {
  // expected-error @+1 {{malformed conditional-expression child inventory}}
  slang.expression.conditional_op attributes {
    condition_count = 1 : i64,
    condition_pattern_flags = array<i64: 0>, node_id = 0 : i64,
    semantic_type = !slang.integral<1, false, true, 0 : 0, logic>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{setter inventory describes 3 children but body contains 0}}
  slang.expression.structured_assignment_pattern attributes {
    has_default_setter = true, index_setter_count = 1 : i64,
    member_setter_count = 0 : i64, node_id = 0 : i64,
    semantic_type = !slang.integral<32, true, false, 31 : 0, int>,
    type_setter_count = 0 : i64
  } {
  }
}

// -----

module {
  // expected-error @+1 {{assertion invocation inventory describes 2 children but body contains 0}}
  slang.expression.assertion_instance attributes {
    argument_count = 1 : i64, argument_formal_paths = ["formal"],
    argument_formal_symbols = [@formal], argument_kinds = array<i64: 0>,
    has_expanded_body = true, is_recursive_property = false,
    local_variable_count = 0 : i64, local_variable_has_initializer = array<i64>,
    local_variable_paths = [], local_variable_symbols = [], node_id = 0 : i64,
    referenced_path = "property", referenced_symbol = @property,
    semantic_type = !slang.property
  } {
  }
}

// -----

module {
  // expected-error @+1 {{iterator loop_dimensions entry #0 requires symbol, nonempty path, and type}}
  slang.statement.foreach_loop attributes {
    loop_dimensions = [{
      has_iterator = true, has_static_range = true,
      iterator_path = "i", left = 3 : i64, right = 0 : i64
    }], node_id = 0 : i64
  } {
    slang.statement.empty attributes {node_id = 1 : i64} {
    }
    slang.statement.empty attributes {node_id = 2 : i64} {
    }
  }
}

// -----

module {
  // expected-error @+1 {{malformed for-loop child inventory}}
  slang.statement.for_loop attributes {
    has_condition = false, initializer_count = 1 : i64,
    node_id = 0 : i64, step_count = 0 : i64
  } {
    slang.statement.empty attributes {node_id = 1 : i64} {
    }
  }
}

// -----

module {
  // expected-error @+1 {{initializer_count must be nonnegative}}
  slang.statement.for_loop attributes {
    has_condition = false, initializer_count = -1 : i64,
    node_id = 0 : i64, step_count = 0 : i64
  } {
    slang.statement.empty attributes {node_id = 1 : i64} {
    }
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
    condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>,
    has_else = false, node_id = 0 : i64
  } {
  }
}

// -----

module {
  // expected-error @+1 {{condition_pattern_flags must contain one entry per inventory item}}
  slang.statement.conditional attributes {
    check_kind = 0 : i32, condition_count = 1 : i64,
    condition_pattern_flags = array<i64>, has_else = false, node_id = 0 : i64
  } {
  }
}

// -----

module {
  // expected-error @+1 {{every case item must contain at least one label}}
  slang.statement.case attributes {
    check_kind = 0 : i32, condition_kind = 0 : i32, has_default = false,
    item_count = 1 : i64, item_label_counts = array<i64: 0>,
    node_id = 0 : i64
  } {
  }
}

// -----

module {
  // expected-error @+1 {{item_filter_flags entries must be zero or one}}
  slang.statement.pattern_case attributes {
    check_kind = 0 : i32, condition_kind = 0 : i32, has_default = false,
    item_count = 1 : i64, item_filter_flags = array<i64: 2>,
    node_id = 0 : i64
  } {
  }
}

// -----

module {
  // expected-error @+1 {{pattern case cannot use the case-inside matching mode}}
  slang.statement.pattern_case attributes {
    check_kind = 0 : i32, condition_kind = 3 : i32, has_default = false,
    item_count = 0 : i64, item_filter_flags = array<i64>, node_id = 0 : i64
  } {
  }
}

// -----

module {
  // expected-error @+1 {{inside set must contain at least one item}}
  slang.expression.inside attributes {
    item_count = 0 : i64, node_id = 0 : i64,
    semantic_type = !slang.integral<1, false, true, 0 : 0, logic>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{inside item inventory overflows}}
  slang.expression.inside attributes {
    item_count = -1 : i64, node_id = 0 : i64,
    semantic_type = !slang.integral<1, false, true, 0 : 0, logic>
  } {
  }
}

// -----

module {
  // expected-error @+1 {{value range must contain exactly two endpoints}}
  slang.expression.value_range attributes {
    node_id = 0 : i64, range_kind = 0 : i32, semantic_type = !slang.void
  } {
  }
}

// -----

module {
  // expected-error @+1 {{structure pattern field ordinals must be nonnegative and unique}}
  slang.pattern.structure attributes {
    field_ordinals = array<i64: 0, 0>, node_id = 0 : i64
  } {
    slang.pattern.wildcard attributes {node_id = 1 : i64} {
    }
    slang.pattern.wildcard attributes {node_id = 2 : i64} {
    }
  }
}

// -----

module {
  // expected-error @+1 {{pattern variable must have a resolved binding path}}
  slang.pattern.variable attributes {
    node_id = 0 : i64, referenced_path = "", referenced_symbol = @missing
  } {
  }
}

// -----

module {
  slang.symbol.root @root attributes {node_id = 0 : i64} {
    // expected-error @+1 {{cannot resolve "referenced_symbol" @missing}}
    slang.pattern.variable attributes {
      node_id = 1 : i64, referenced_path = "missing", referenced_symbol = @missing
    } {
    }
  }
}

// -----

module {
  slang.symbol.root @root attributes {node_id = 0 : i64} {
    slang.symbol.variable @ordinary attributes {
      lifetime = 0 : i32, node_id = 1 : i64, rand_mode = 0 : i32,
      semantic_type = !slang.integral<4, false, true, 3 : 0, logic>
    } {
    }
    // expected-error @+1 {{referenced pattern variable does not resolve to a pattern binding}}
    slang.pattern.variable attributes {
      node_id = 2 : i64, referenced_path = "ordinary",
      referenced_symbol = @ordinary
    } {
    }
  }
}

// -----

module {
  // expected-error @+1 {{tagged pattern must have a nonnegative field ordinal}}
  slang.pattern.tagged attributes {
    node_id = 0 : i64, packed_offset = 0 : i64,
    referenced_path = "missing.member", referenced_symbol = @missing
  } {
  }
}

// -----

module {
  slang.symbol.root @root attributes {node_id = 0 : i64} {
    // expected-error @+1 {{cannot resolve "referenced_symbol" @missing}}
    slang.pattern.tagged attributes {
      field_ordinal = 0 : i64, node_id = 1 : i64, packed_offset = 0 : i64,
      referenced_path = "missing.member", referenced_symbol = @missing
    } {
    }
  }
}

// -----

module {
  slang.symbol.root @root attributes {node_id = 0 : i64} {
    slang.symbol.field @member attributes {
      bit_offset = 0 : i64, field_index = 1 : i64, lifetime = 0 : i32,
      node_id = 1 : i64, rand_mode = 0 : i32,
      semantic_type = !slang.integral<4, false, true, 3 : 0, logic>
    } {
    }
    // expected-error @+1 {{tagged pattern field metadata does not match its referenced member}}
    slang.pattern.tagged attributes {
      field_ordinal = 0 : i64, node_id = 2 : i64, packed_offset = 0 : i64,
      referenced_path = "member", referenced_symbol = @member
    } {
    }
  }
}

// -----

module {
  // Generic syntax is needed to omit the required symbol name.
  // expected-error @+1 {{requires attribute 'sym_name'}}
  "slang.type.string_type"() ({
  }) {node_id = 0 : i64, semantic_type = !slang.string} : () -> ()
}

// -----

module {
  // expected-error @+1 {{represents an invalid semantic sentinel}}
  slang.constraint.invalid attributes {node_id = 0 : i64} {
  }
}

// -----

module {
  slang.symbol.variable @bad_error attributes {
    node_id = 0 : i64,
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
  slang.symbol.variable @bad_range attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{declared range width exceeds uint64_t}}
    semantic_type = !slang.integral<1, true, true, 9223372036854775807 : -9223372036854775808, generic>
  } {
  }
}

// -----

module {
  slang.symbol.variable @bad_element attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{packed array element must be packed}}
    semantic_type = !slang.packed_array<3 : 0 x !slang.string>
  } {
  }
}

// -----

module {
  slang.symbol.variable @bad_index attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{wildcard associative index must use !slang.untyped}}
    semantic_type = !slang.associative_array<!slang.string, !slang.real, true>
  } {
  }
}

// -----

module {
  slang.symbol.variable @bad_aggregate attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{only a union can be tagged}}
    semantic_type = !slang.aggregate<"record_t", false, false, true, false, false, false, 0, 0, 0, 0, []>
  } {
  }
}

// -----

module {
  slang.symbol.variable @missing_field_metadata attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{aggregate fields require name, type, ordinal, and packed_offset metadata}}
    semantic_type = !slang.aggregate<"record_t", false, false, false, false, false, false, 0, 0, 0, 0, [{name = "a"}]>
  } {
  }
}

// -----

module {
  slang.symbol.variable @bad_field_ordinal attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{aggregate field ordinals must be dense and ordered}}
    semantic_type = !slang.aggregate<"record_t", false, false, false, false, false, false, 0, 0, 0, 0, [{name = "a", ordinal = 1 : i32, packed_offset = 0 : i64, type = !slang.integral<8, false, false, 7 : 0, bit>}]>
  } {
  }
}

// -----

module {
  slang.symbol.variable @duplicate_field_name attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{aggregate field names must be unique}}
    semantic_type = !slang.aggregate<"record_t", false, false, false, false, false, false, 0, 0, 0, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, type = !slang.integral<8, false, false, 7 : 0, bit>}, {name = "a", ordinal = 1 : i32, packed_offset = 0 : i64, type = !slang.integral<8, false, false, 7 : 0, bit>}]>
  } {
  }
}

// -----

module {
  slang.symbol.variable @negative_field_offset attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{aggregate field has invalid packed offset}}
    semantic_type = !slang.aggregate<"record_t", true, false, false, false, false, false, 8, 8, 8, 0, [{name = "a", ordinal = 0 : i32, packed_offset = -1 : i64, type = !slang.integral<8, false, false, 7 : 0, bit>}]>
  } {
  }
}

// -----

module {
  slang.symbol.variable @unpacked_field_offset attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{aggregate field has invalid packed offset}}
    semantic_type = !slang.aggregate<"record_t", false, false, false, false, false, false, 0, 0, 0, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 1 : i64, type = !slang.integral<8, false, false, 7 : 0, bit>}]>
  } {
  }
}

// -----

module {
  slang.symbol.variable @bad_field_rand_mode attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{aggregate field has invalid randomization mode}}
    semantic_type = !slang.aggregate<"record_t", false, false, false, false, false, false, 0, 0, 0, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 3 : i32, type = !slang.integral<8, false, false, 7 : 0, bit>}]>
  } {
  }
}

// -----

module {
  slang.symbol.subroutine @bad_signature attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{subroutine signature must be a function type}}
    semantic_type = !slang.subroutine<!slang.string, false>
  } {
  }
}

// -----

module {
  slang.symbol.variable @bad_source_range attributes {
    node_id = 0 : i64,
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
  slang.symbol.variable @bad_declared_range attributes {
    node_id = 0 : i64,
    // expected-error @+1 {{declared range width 4 does not match integral width 8}}
    semantic_type = !slang.integral<8, false, true, 3 : 0, generic>
  } {
  }
}
