// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  // expected-error @below {{a cross requires at least two targets}}
  "obelisk.sv.symbol.cover_cross"() ({
  }) {expression_roles = [], has_iff = false,
      hierarchical_name = "m.c", node_id = 0 : i64, option_count = 0 : i64,
      sym_name = "c", target_count = 1 : i64, target_symbols = [@cp]}
      : () -> ()
}

// -----

module {
  "obelisk.sv.symbol.root"() ({
    "obelisk.sv.symbol.coverpoint"() ({
      "obelisk.sv.expression.integer_literal"() ({
      }) {constant_value = "0", node_id = 2 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>}
          : () -> ()
      // expected-error @below {{owner_symbol must name the option's actual enclosing owner}}
      "obelisk.sv.coverage.option"() ({
        "obelisk.sv.expression.integer_literal"() ({
        }) {constant_value = "1", node_id = 4 : i64,
            semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>}
            : () -> ()
      }) {node_id = 3 : i64, option_kind = 1 : i32, owner_kind = 2 : i32,
          owner_symbol = @cp, scope_kind = 0 : i32} : () -> ()
    }) {expression_roles = [0 : i32], has_iff = false, node_id = 1 : i64,
        option_count = 1 : i64,
        semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
        sym_name = "cp"} : () -> ()
  }) {node_id = 0 : i64, sym_name = "root"} : () -> ()
}

// -----

module {
  "obelisk.sv.symbol.root"() ({
    // expected-error @below {{cannot resolve "constructor_formals" @wrong::@g::@f}}
    "obelisk.sv.type.covergroup_type"() ({
      "obelisk.sv.symbol.formal_argument"() ({
      }) {direction = 0 : i32, node_id = 2 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "f"} : () -> ()
    }) {constructor_argument_count = 1 : i64,
        constructor_formals = [@wrong::@g::@f],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g>,
        sym_name = "g"} : () -> ()
  }) {node_id = 0 : i64, sym_name = "root"} : () -> ()
}

// -----

module {
  // expected-error @below {{must contain exactly one option value expression}}
  "obelisk.sv.coverage.option"() ({
  }) {node_id = 0 : i64, option_kind = 1 : i32, owner_kind = 1 : i32,
      owner_symbol = @cp, scope_kind = 0 : i32} : () -> ()
}

// -----

module {
  // expected-error @below {{binary selector requires two selector children}}
  "obelisk.sv.bins.binary"() ({
  }) {enclosing_cross_symbol = @cross, node_id = 0 : i64,
      operator_kind = 0 : i32} : () -> ()
}

// -----

module {
  // expected-error @below {{clocking-event form requires one legal clocking-event timing child}}
  "obelisk.sv.type.covergroup_type"() ({
  }) {constructor_argument_count = 0 : i64, constructor_formals = [],
      coverage_event_kind = 1 : i32, has_coverage_event = false,
      node_id = 0 : i64, sample_formal_count = 0 : i64, sample_formals = [],
      semantic_type = !obelisk.covergroup_handle<@cg>, sym_name = "cg"}
      : () -> ()
}

// -----

module {
  "obelisk.sv.symbol.root"() ({
    "obelisk.sv.type.covergroup_type"() ({
      "obelisk.sv.symbol.formal_argument"() ({
      }) {direction = 0 : i32, node_id = 2 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "foreign"} : () -> ()
    }) {constructor_argument_count = 1 : i64,
        constructor_formals = [@root::@g1::@foreign],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g1>,
        sym_name = "g1"} : () -> ()
    // expected-error @below {{constructor_formals must be direct formals of this covergroup or its base-group chain}}
    "obelisk.sv.type.covergroup_type"() ({
    }) {constructor_argument_count = 1 : i64,
        constructor_formals = [@root::@g1::@foreign],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 3 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g2>,
        sym_name = "g2"} : () -> ()
  }) {node_id = 0 : i64, sym_name = "root"} : () -> ()
}

// -----

module {
  "obelisk.sv.symbol.root"() ({
    "obelisk.sv.type.covergroup_type"() ({
      "obelisk.sv.symbol.coverpoint"() ({
        "obelisk.sv.expression.integer_literal"() ({
        }) {constant_value = "0", node_id = 3 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>}
            : () -> ()
        // expected-error @below {{owner_symbol must name the option's actual enclosing owner}}
        "obelisk.sv.coverage.option"() ({
          "obelisk.sv.expression.integer_literal"() ({
          }) {constant_value = "1", node_id = 5 : i64,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>}
              : () -> ()
        }) {node_id = 4 : i64, option_kind = 1 : i32, owner_kind = 1 : i32,
            owner_symbol = @root::@g::@p2, scope_kind = 0 : i32} : () -> ()
        // expected-error @below {{owner_symbol must name the option's actual enclosing owner}}
        "obelisk.sv.coverage.option"() ({
          "obelisk.sv.expression.integer_literal"() ({
          }) {constant_value = "1", node_id = 9 : i64,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>}
              : () -> ()
        }) {node_id = 8 : i64, option_kind = 1 : i32, owner_kind = 0 : i32,
            owner_symbol = @root::@g, scope_kind = 0 : i32} : () -> ()
      }) {expression_roles = [0 : i32], has_iff = false, node_id = 2 : i64,
          option_count = 2 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
          sym_name = "p1"} : () -> ()
      "obelisk.sv.symbol.coverpoint"() ({
        "obelisk.sv.expression.integer_literal"() ({
        }) {constant_value = "0", node_id = 7 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>}
            : () -> ()
      }) {expression_roles = [0 : i32], has_iff = false, node_id = 6 : i64,
          option_count = 0 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
          sym_name = "p2"} : () -> ()
    }) {constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g>,
        sym_name = "g"} : () -> ()
  }) {node_id = 0 : i64, sym_name = "root"} : () -> ()
}

// -----

module {
  "obelisk.sv.symbol.root"() ({
    "obelisk.sv.type.covergroup_type"() ({
      "obelisk.sv.symbol.coverpoint"() ({
        "obelisk.sv.expression.integer_literal"() ({
        }) {constant_value = "0", node_id = 3 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>}
            : () -> ()
      }) {expression_roles = [0 : i32], has_iff = false, node_id = 2 : i64,
          option_count = 0 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
          sym_name = "p1"} : () -> ()
      // expected-error @below {{cross target must belong to its enclosing covergroup or base-group chain}}
      "obelisk.sv.symbol.cover_cross"() ({
      }) {expression_roles = [], has_iff = false, node_id = 4 : i64,
          option_count = 0 : i64, sym_name = "x",
          target_count = 2 : i64,
          target_symbols = [@root::@g1::@p1, @root::@g2::@p2]} : () -> ()
    }) {constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g1>,
        sym_name = "g1"} : () -> ()
    "obelisk.sv.type.covergroup_type"() ({
      "obelisk.sv.symbol.coverpoint"() ({
        "obelisk.sv.expression.integer_literal"() ({
        }) {constant_value = "0", node_id = 7 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>}
            : () -> ()
      }) {expression_roles = [0 : i32], has_iff = false, node_id = 6 : i64,
          option_count = 0 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
          sym_name = "p2"} : () -> ()
    }) {constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 5 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g2>,
        sym_name = "g2"} : () -> ()
  }) {node_id = 0 : i64, sym_name = "root"} : () -> ()
}

// -----

module {
  "obelisk.sv.symbol.root"() ({
    "obelisk.sv.type.covergroup_type"() ({
      "obelisk.sv.symbol.coverpoint"() ({
        "obelisk.sv.expression.integer_literal"() ({
        }) {constant_value = "0", node_id = 3 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>}
            : () -> ()
      }) {expression_roles = [0 : i32], has_iff = false, node_id = 2 : i64,
          option_count = 0 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
          sym_name = "p1"} : () -> ()
      "obelisk.sv.symbol.coverpoint"() ({
        "obelisk.sv.expression.integer_literal"() ({
        }) {constant_value = "0", node_id = 5 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>}
            : () -> ()
      }) {expression_roles = [0 : i32], has_iff = false, node_id = 4 : i64,
          option_count = 0 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
          sym_name = "p2"} : () -> ()
      "obelisk.sv.symbol.cover_cross"() ({
        // expected-error @below {{enclosing_cross_symbol must name the selector's enclosing cross}}
        "obelisk.sv.bins.condition"() ({
        }) {enclosing_cross_symbol = @root::@g::@x2, intersect_count = 0 : i64,
            node_id = 7 : i64, target_symbol = @root::@g::@p1} : () -> ()
      }) {expression_roles = [], has_iff = false, node_id = 6 : i64,
          option_count = 0 : i64, sym_name = "x1", target_count = 2 : i64,
          target_symbols = [@root::@g::@p1, @root::@g::@p2]} : () -> ()
      "obelisk.sv.symbol.cover_cross"() ({
      }) {expression_roles = [], has_iff = false, node_id = 8 : i64,
          option_count = 0 : i64, sym_name = "x2", target_count = 2 : i64,
          target_symbols = [@root::@g::@p1, @root::@g::@p2]} : () -> ()
    }) {constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g>,
        sym_name = "g"} : () -> ()
  }) {node_id = 0 : i64, sym_name = "root"} : () -> ()
}

// -----

module {
  "obelisk.sv.symbol.root"() ({
    "obelisk.sv.type.covergroup_type"() ({
      "obelisk.sv.symbol.coverpoint"() ({
        "obelisk.sv.expression.integer_literal"() ({
        }) {constant_value = "0", node_id = 3 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>}
            : () -> ()
      }) {expression_roles = [0 : i32], has_iff = false, node_id = 2 : i64,
          option_count = 0 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
          sym_name = "p1"} : () -> ()
      "obelisk.sv.symbol.coverpoint"() ({
        "obelisk.sv.expression.integer_literal"() ({
        }) {constant_value = "0", node_id = 5 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>}
            : () -> ()
      }) {expression_roles = [0 : i32], has_iff = false, node_id = 4 : i64,
          option_count = 0 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
          sym_name = "p2"} : () -> ()
      "obelisk.sv.symbol.coverpoint"() ({
        "obelisk.sv.expression.integer_literal"() ({
        }) {constant_value = "0", node_id = 7 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>}
            : () -> ()
      }) {expression_roles = [0 : i32], has_iff = false, node_id = 6 : i64,
          option_count = 0 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
          sym_name = "p3"} : () -> ()
      "obelisk.sv.symbol.cover_cross"() ({
        // expected-error @below {{binsof target must be one of the enclosing cross targets}}
        "obelisk.sv.bins.condition"() ({
        }) {enclosing_cross_symbol = @root::@g::@x, intersect_count = 0 : i64,
            node_id = 9 : i64, target_symbol = @root::@g::@p3} : () -> ()
      }) {expression_roles = [], has_iff = false, node_id = 8 : i64,
          option_count = 0 : i64, sym_name = "x", target_count = 2 : i64,
          target_symbols = [@root::@g::@p1, @root::@g::@p2]} : () -> ()
    }) {constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g>,
        sym_name = "g"} : () -> ()
  }) {node_id = 0 : i64, sym_name = "root"} : () -> ()
}

// -----

module {
  "obelisk.sv.symbol.root"() ({
    "obelisk.sv.type.covergroup_type"() ({
      "obelisk.sv.timing.signal_event"() ({
      }) {edge_kind = 1 : i32, has_iff = false, node_id = 2 : i64} : () -> ()
    }) {constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 1 : i32, has_coverage_event = true,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@base>,
        sym_name = "base"} : () -> ()
    // expected-error @below {{inherited sampling form cannot duplicate its base timing child}}
    "obelisk.sv.type.covergroup_type"() ({
      "obelisk.sv.timing.signal_event"() ({
      }) {edge_kind = 2 : i32, has_iff = false, node_id = 4 : i64} : () -> ()
    }) {base_group = !obelisk.covergroup_handle<@root::@base>,
        constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 4 : i32, has_coverage_event = true,
        node_id = 3 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@derived>,
        sym_name = "derived"} : () -> ()
  }) {node_id = 0 : i64, sym_name = "root"} : () -> ()
}

// -----

module attributes {obelisk.coverage.language_version = 2017 : i32} {
  // expected-error @below {{option is not legal for this coverage owner and option scope}}
  "obelisk.sv.coverage.option"() ({
    "obelisk.sv.expression.integer_literal"() ({
    }) {constant_value = "1", node_id = 1 : i64,
        semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>}
        : () -> ()
  }) {node_id = 0 : i64, option_kind = 8 : i32, owner_kind = 1 : i32,
      owner_symbol = @cp, scope_kind = 1 : i32} : () -> ()
}

// -----

// Coverage relationship references use the nearest semantic root even when
// another root in the same module contains identical leaf names.
module {
  "obelisk.sv.symbol.root"() ({
    "obelisk.sv.type.covergroup_type"() ({
      "obelisk.sv.symbol.formal_argument"() ({
      }) {direction = 0 : i32, node_id = 2 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "f"} : () -> ()
    }) {constructor_argument_count = 1 : i64, constructor_formals = [@f],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@g>,
        sym_name = "g"} : () -> ()
  }) {node_id = 0 : i64, sym_name = "root_a"} : () -> ()
  "obelisk.sv.symbol.root"() ({
    "obelisk.sv.type.covergroup_type"() ({
      "obelisk.sv.symbol.formal_argument"() ({
      }) {direction = 0 : i32, node_id = 5 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "f"} : () -> ()
    }) {constructor_argument_count = 1 : i64, constructor_formals = [@f],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 4 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@g>,
        sym_name = "g"} : () -> ()
  }) {node_id = 3 : i64, sym_name = "root_b"} : () -> ()
}

// -----

// A generic semantic reference cannot escape its root and bind to a matching
// leaf in another root.
module {
  "obelisk.sv.symbol.root"() ({
    // expected-error @below {{cannot resolve "referenced_symbol" @binding}}
    "obelisk.sv.pattern.variable"() ({
    }) {node_id = 1 : i64, referenced_path = "binding",
        referenced_symbol = @binding} : () -> ()
  }) {node_id = 0 : i64, sym_name = "root_a"} : () -> ()
  "obelisk.sv.symbol.root"() ({
    "obelisk.sv.symbol.pattern_var"() ({
    }) {lifetime = 0 : i32, node_id = 3 : i64, rand_mode = 0 : i32,
        semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
        sym_name = "binding"} : () -> ()
  }) {node_id = 2 : i64, sym_name = "root_b"} : () -> ()
}

// -----

// An unrelated target_symbol remains a generic semantic reference and may
// resolve to a module-sibling declaration.
module {
  "obelisk.sv.symbol.variable"() ({
  }) {lifetime = 0 : i32, node_id = 0 : i64, rand_mode = 0 : i32,
      semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
      sym_name = "external_target"} : () -> ()
  "obelisk.sv.symbol.root"() ({
    "obelisk.sv.statement.disable"() ({
    }) {is_hierarchical = false, node_id = 2 : i64,
        target_path = "external_target", target_symbol = @external_target}
        : () -> ()
  }) {node_id = 1 : i64, sym_name = "root"} : () -> ()
}

// -----

module {
  "obelisk.sv.symbol.root"() ({
    // expected-error @below {{constructor_formals cannot name coverage sample formals}}
    // expected-error @below {{sample_formals must be direct formals of this covergroup or its base-group chain}}
    "obelisk.sv.type.covergroup_type"() ({
      "obelisk.sv.symbol.formal_argument"() ({
      }) {direction = 0 : i32, is_coverage_sample_formal, node_id = 2 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "sample_formal"} : () -> ()
      "obelisk.sv.symbol.covergroup_body"() ({
        "obelisk.sv.symbol.formal_argument"() ({
        }) {direction = 0 : i32, is_coverage_sample_formal, node_id = 4 : i64,
            semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
            sym_name = "copied_sample_formal"} : () -> ()
      }) {node_id = 3 : i64, option_count = 0 : i64, sym_name = "body"}
          : () -> ()
    }) {constructor_argument_count = 1 : i64,
        constructor_formals = [@root::@g::@sample_formal],
        coverage_event_kind = 2 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 1 : i64,
        sample_formals = [@root::@g::@body::@copied_sample_formal],
        semantic_type = !obelisk.covergroup_handle<@root::@g>, sym_name = "g"}
        : () -> ()
  }) {node_id = 0 : i64, sym_name = "root"} : () -> ()
}

// -----

module {
  // expected-error @below {{clocking-event form requires one legal clocking-event timing child}}
  "obelisk.sv.type.covergroup_type"() ({
    "obelisk.sv.timing.delay"() ({
    }) {node_id = 1 : i64} : () -> ()
  }) {constructor_argument_count = 0 : i64, constructor_formals = [],
      coverage_event_kind = 1 : i32, has_coverage_event = true,
      node_id = 0 : i64, sample_formal_count = 0 : i64, sample_formals = [],
      semantic_type = !obelisk.covergroup_handle<@g>, sym_name = "g"}
      : () -> ()
}

// -----

module attributes {obelisk.coverage.language_version = 2017 : i32} {
  "obelisk.sv.symbol.root"() ({
    "obelisk.sv.type.covergroup_type"() ({
    }) {constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@base>,
        sym_name = "base"} : () -> ()
    // expected-error @below {{covergroup inheritance requires IEEE 1800-2023 or later}}
    "obelisk.sv.type.covergroup_type"() ({
    }) {base_group = !obelisk.covergroup_handle<@root::@base>,
        constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 4 : i32, has_coverage_event = false,
        node_id = 2 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@derived>,
        sym_name = "derived"} : () -> ()
  }) {node_id = 0 : i64, sym_name = "root"} : () -> ()
}

// -----

module attributes {obelisk.coverage.language_version = 2023 : i32} {
  "obelisk.sv.symbol.root"() ({
    // expected-error @below {{base_group chain must be acyclic}}
    "obelisk.sv.type.covergroup_type"() ({
    }) {base_group = !obelisk.covergroup_handle<@root::@self>,
        constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 4 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@self>,
        sym_name = "self"} : () -> ()
  }) {node_id = 0 : i64, sym_name = "root"} : () -> ()
}

// -----

// The same typed option is legal in the IEEE 1800-2023 semantic context.
module attributes {obelisk.coverage.language_version = 2023 : i32} {
  "obelisk.sv.symbol.root"() ({
    "obelisk.sv.type.covergroup_type"() ({
      "obelisk.sv.coverage.option"() ({
        "obelisk.sv.expression.real_literal"() ({
        }) {constant_value = "0.25", node_id = 3 : i64,
            semantic_type = !obelisk.real} : () -> ()
      }) {node_id = 2 : i64, option_kind = 14 : i32, owner_kind = 0 : i32,
          owner_symbol = @root::@g, scope_kind = 1 : i32} : () -> ()
    }) {constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g>,
        sym_name = "g"} : () -> ()
  }) {node_id = 0 : i64, sym_name = "root"} : () -> ()
}

// -----

module attributes {obelisk.coverage.language_version = 2017 : i32} {
  "obelisk.sv.symbol.root"() ({
    "obelisk.sv.type.covergroup_type"() ({
      // expected-error @below {{coverage option requires IEEE 1800-2023 or later}}
      "obelisk.sv.coverage.option"() ({
        "obelisk.sv.expression.real_literal"() ({
        }) {constant_value = "0.25", node_id = 3 : i64,
            semantic_type = !obelisk.real} : () -> ()
      }) {node_id = 2 : i64, option_kind = 14 : i32, owner_kind = 0 : i32,
          owner_symbol = @root::@g, scope_kind = 1 : i32} : () -> ()
    }) {constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g>,
        sym_name = "g"} : () -> ()
  }) {node_id = 0 : i64, sym_name = "root"} : () -> ()
}
