// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  // expected-error @below {{a cross requires at least two targets}}
  obelisk.sv.symbol.cover_cross @c attributes {expression_roles = [], has_iff = false,
      hierarchical_name = "m.c", node_id = 0 : i64, option_count = 0 : i64,
      target_count = 1 : i64, target_symbols = [@cp]} {
  }
}

// -----

module {
  obelisk.sv.symbol.root @root attributes {node_id = 0 : i64} {
    obelisk.sv.symbol.coverpoint @cp attributes {expression_roles = [0 : i32], has_iff = false, node_id = 1 : i64,
        option_count = 1 : i64,
        semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
    } {
      obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 2 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
      }
      // expected-error @below {{owner_symbol must name the option's actual enclosing owner}}
      obelisk.sv.coverage.option attributes {node_id = 3 : i64, option_kind = 1 : i32, owner_kind = 2 : i32,
          owner_symbol = @cp, scope_kind = 0 : i32} {
        obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 4 : i64,
            semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
      }
    }
  }
}

// -----

module {
  obelisk.sv.symbol.root @root attributes {node_id = 0 : i64} {
    // expected-error @below {{cannot resolve "constructor_formals" @wrong::@g::@f}}
    obelisk.sv.type.covergroup_type @g attributes {constructor_argument_count = 1 : i64,
        constructor_formals = [@wrong::@g::@f],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g>
    } {
      obelisk.sv.symbol.formal_argument @f attributes {direction = 0 : i32, node_id = 2 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
      } {
      }
    }
  }
}

// -----

module {
  // expected-error @below {{must contain exactly one option value expression}}
  obelisk.sv.coverage.option attributes {node_id = 0 : i64, option_kind = 1 : i32, owner_kind = 1 : i32,
      owner_symbol = @cp, scope_kind = 0 : i32} {
  }
}

// -----

module {
  // expected-error @below {{binary selector requires two selector children}}
  obelisk.sv.bins.binary attributes {enclosing_cross_symbol = @cross, node_id = 0 : i64,
      operator_kind = 0 : i32} {
  }
}

// -----

module {
  // expected-error @below {{clocking-event form requires one legal clocking-event timing child}}
  obelisk.sv.type.covergroup_type @cg attributes {constructor_argument_count = 0 : i64, constructor_formals = [],
      coverage_event_kind = 1 : i32, has_coverage_event = false,
      node_id = 0 : i64, sample_formal_count = 0 : i64, sample_formals = [],
      semantic_type = !obelisk.covergroup_handle<@cg>} {
  }
}

// -----

module {
  obelisk.sv.symbol.root @root attributes {node_id = 0 : i64} {
    obelisk.sv.type.covergroup_type @g1 attributes {constructor_argument_count = 1 : i64,
        constructor_formals = [@root::@g1::@foreign],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g1>
    } {
      obelisk.sv.symbol.formal_argument @foreign attributes {direction = 0 : i32, node_id = 2 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
      } {
      }
    }
    // expected-error @below {{constructor_formals must be direct formals of this covergroup or its base-group chain}}
    obelisk.sv.type.covergroup_type @g2 attributes {constructor_argument_count = 1 : i64,
        constructor_formals = [@root::@g1::@foreign],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 3 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g2>
    } {
    }
  }
}

// -----

module {
  obelisk.sv.symbol.root @root attributes {node_id = 0 : i64} {
    obelisk.sv.type.covergroup_type @g attributes {constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g>
    } {
      obelisk.sv.symbol.coverpoint @p1 attributes {expression_roles = [0 : i32], has_iff = false, node_id = 2 : i64,
          option_count = 2 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
      } {
        obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 3 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        // expected-error @below {{owner_symbol must name the option's actual enclosing owner}}
        obelisk.sv.coverage.option attributes {node_id = 4 : i64, option_kind = 1 : i32, owner_kind = 1 : i32,
            owner_symbol = @root::@g::@p2, scope_kind = 0 : i32} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 5 : i64,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        // expected-error @below {{owner_symbol must name the option's actual enclosing owner}}
        obelisk.sv.coverage.option attributes {node_id = 8 : i64, option_kind = 1 : i32, owner_kind = 0 : i32,
            owner_symbol = @root::@g, scope_kind = 0 : i32} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 9 : i64,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
      obelisk.sv.symbol.coverpoint @p2 attributes {expression_roles = [0 : i32], has_iff = false, node_id = 6 : i64,
          option_count = 0 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
      } {
        obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 7 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
      }
    }
  }
}

// -----

module {
  obelisk.sv.symbol.root @root attributes {node_id = 0 : i64} {
    obelisk.sv.type.covergroup_type @g1 attributes {constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g1>
    } {
      obelisk.sv.symbol.coverpoint @p1 attributes {expression_roles = [0 : i32], has_iff = false, node_id = 2 : i64,
          option_count = 0 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
      } {
        obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 3 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
      }
      // expected-error @below {{cross target must belong to its enclosing covergroup or base-group chain}}
      obelisk.sv.symbol.cover_cross @x attributes {expression_roles = [], has_iff = false, node_id = 4 : i64,
          option_count = 0 : i64,
          target_count = 2 : i64,
          target_symbols = [@root::@g1::@p1, @root::@g2::@p2]} {
      }
    }
    obelisk.sv.type.covergroup_type @g2 attributes {constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 5 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g2>
    } {
      obelisk.sv.symbol.coverpoint @p2 attributes {expression_roles = [0 : i32], has_iff = false, node_id = 6 : i64,
          option_count = 0 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
      } {
        obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 7 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
      }
    }
  }
}

// -----

module {
  obelisk.sv.symbol.root @root attributes {node_id = 0 : i64} {
    obelisk.sv.type.covergroup_type @g attributes {constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g>
    } {
      obelisk.sv.symbol.coverpoint @p1 attributes {expression_roles = [0 : i32], has_iff = false, node_id = 2 : i64,
          option_count = 0 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
      } {
        obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 3 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
      }
      obelisk.sv.symbol.coverpoint @p2 attributes {expression_roles = [0 : i32], has_iff = false, node_id = 4 : i64,
          option_count = 0 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
      } {
        obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 5 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
      }
      obelisk.sv.symbol.cover_cross @x1 attributes {expression_roles = [], has_iff = false, node_id = 6 : i64,
          option_count = 0 : i64, target_count = 2 : i64,
          target_symbols = [@root::@g::@p1, @root::@g::@p2]} {
        // expected-error @below {{enclosing_cross_symbol must name the selector's enclosing cross}}
        obelisk.sv.bins.condition attributes {enclosing_cross_symbol = @root::@g::@x2, intersect_count = 0 : i64,
            node_id = 7 : i64, target_symbol = @root::@g::@p1} {
        }
      }
      obelisk.sv.symbol.cover_cross @x2 attributes {expression_roles = [], has_iff = false, node_id = 8 : i64,
          option_count = 0 : i64, target_count = 2 : i64,
          target_symbols = [@root::@g::@p1, @root::@g::@p2]} {
      }
    }
  }
}

// -----

module {
  obelisk.sv.symbol.root @root attributes {node_id = 0 : i64} {
    obelisk.sv.type.covergroup_type @g attributes {constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g>
    } {
      obelisk.sv.symbol.coverpoint @p1 attributes {expression_roles = [0 : i32], has_iff = false, node_id = 2 : i64,
          option_count = 0 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
      } {
        obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 3 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
      }
      obelisk.sv.symbol.coverpoint @p2 attributes {expression_roles = [0 : i32], has_iff = false, node_id = 4 : i64,
          option_count = 0 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
      } {
        obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 5 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
      }
      obelisk.sv.symbol.coverpoint @p3 attributes {expression_roles = [0 : i32], has_iff = false, node_id = 6 : i64,
          option_count = 0 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
      } {
        obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 7 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
      }
      obelisk.sv.symbol.cover_cross @x attributes {expression_roles = [], has_iff = false, node_id = 8 : i64,
          option_count = 0 : i64, target_count = 2 : i64,
          target_symbols = [@root::@g::@p1, @root::@g::@p2]} {
        // expected-error @below {{binsof target must be one of the enclosing cross targets}}
        obelisk.sv.bins.condition attributes {enclosing_cross_symbol = @root::@g::@x, intersect_count = 0 : i64,
            node_id = 9 : i64, target_symbol = @root::@g::@p3} {
        }
      }
    }
  }
}

// -----

module {
  obelisk.sv.symbol.root @root attributes {node_id = 0 : i64} {
    obelisk.sv.type.covergroup_type @base attributes {constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 1 : i32, has_coverage_event = true,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@base>
    } {
      obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 2 : i64} {
      }
    }
    // expected-error @below {{inherited sampling form cannot duplicate its base timing child}}
    obelisk.sv.type.covergroup_type @derived attributes {base_group = !obelisk.covergroup_handle<@root::@base>,
        constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 4 : i32, has_coverage_event = true,
        node_id = 3 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@derived>
    } {
      obelisk.sv.timing.signal_event attributes {edge_kind = 2 : i32, has_iff = false, node_id = 4 : i64} {
      }
    }
  }
}

// -----

module attributes {obelisk.coverage.language_version = 2017 : i32} {
  // expected-error @below {{option is not legal for this coverage owner and option scope}}
  obelisk.sv.coverage.option attributes {node_id = 0 : i64, option_kind = 8 : i32, owner_kind = 1 : i32,
      owner_symbol = @cp, scope_kind = 1 : i32} {
    obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 1 : i64,
        semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
    }
  }
}

// -----

// Coverage relationship references use the nearest semantic root even when
// another root in the same module contains identical leaf names.
module {
  obelisk.sv.symbol.root @root_a attributes {node_id = 0 : i64} {
    obelisk.sv.type.covergroup_type @g attributes {constructor_argument_count = 1 : i64, constructor_formals = [@f],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@g>
    } {
      obelisk.sv.symbol.formal_argument @f attributes {direction = 0 : i32, node_id = 2 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
      } {
      }
    }
  }
  obelisk.sv.symbol.root @root_b attributes {node_id = 3 : i64} {
    obelisk.sv.type.covergroup_type @g attributes {constructor_argument_count = 1 : i64, constructor_formals = [@f],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 4 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@g>
    } {
      obelisk.sv.symbol.formal_argument @f attributes {direction = 0 : i32, node_id = 5 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
      } {
      }
    }
  }
}

// -----

// A generic semantic reference cannot escape its root and bind to a matching
// leaf in another root.
module {
  obelisk.sv.symbol.root @root_a attributes {node_id = 0 : i64} {
    // expected-error @below {{cannot resolve "referenced_symbol" @binding}}
    obelisk.sv.pattern.variable attributes {node_id = 1 : i64, referenced_path = "binding",
        referenced_symbol = @binding} {
    }
  }
  obelisk.sv.symbol.root @root_b attributes {node_id = 2 : i64} {
    obelisk.sv.symbol.pattern_var @binding attributes {lifetime = 0 : i32, node_id = 3 : i64, rand_mode = 0 : i32,
        semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
    } {
    }
  }
}

// -----

// An unrelated target_symbol remains a generic semantic reference and may
// resolve to a module-sibling declaration.
module {
  obelisk.sv.symbol.variable @external_target attributes {lifetime = 0 : i32, node_id = 0 : i64, rand_mode = 0 : i32,
      semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
  } {
  }
  obelisk.sv.symbol.root @root attributes {node_id = 1 : i64} {
    obelisk.sv.statement.disable attributes {is_hierarchical = false, node_id = 2 : i64,
        target_path = "external_target", target_symbol = @external_target} {
    }
  }
}

// -----

module {
  obelisk.sv.symbol.root @root attributes {node_id = 0 : i64} {
    // expected-error @below {{constructor_formals cannot name coverage sample formals}}
    // expected-error @below {{sample_formals must be direct formals of this covergroup or its base-group chain}}
    obelisk.sv.type.covergroup_type @g attributes {constructor_argument_count = 1 : i64,
        constructor_formals = [@root::@g::@sample_formal],
        coverage_event_kind = 2 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 1 : i64,
        sample_formals = [@root::@g::@body::@copied_sample_formal],
        semantic_type = !obelisk.covergroup_handle<@root::@g>} {
      obelisk.sv.symbol.formal_argument @sample_formal attributes {direction = 0 : i32, is_coverage_sample_formal, node_id = 2 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
      } {
      }
      obelisk.sv.symbol.covergroup_body @body attributes {node_id = 3 : i64, option_count = 0 : i64} {
        obelisk.sv.symbol.formal_argument @copied_sample_formal attributes {direction = 0 : i32, is_coverage_sample_formal, node_id = 4 : i64,
            semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
        } {
        }
      }
    }
  }
}

// -----

module {
  // expected-error @below {{clocking-event form requires one legal clocking-event timing child}}
  obelisk.sv.type.covergroup_type @g attributes {constructor_argument_count = 0 : i64, constructor_formals = [],
      coverage_event_kind = 1 : i32, has_coverage_event = true,
      node_id = 0 : i64, sample_formal_count = 0 : i64, sample_formals = [],
      semantic_type = !obelisk.covergroup_handle<@g>} {
    obelisk.sv.timing.delay attributes {node_id = 1 : i64} {
    }
  }
}

// -----

module attributes {obelisk.coverage.language_version = 2017 : i32} {
  obelisk.sv.symbol.root @root attributes {node_id = 0 : i64} {
    obelisk.sv.type.covergroup_type @base attributes {constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@base>
    } {
    }
    // expected-error @below {{covergroup inheritance requires IEEE 1800-2023 or later}}
    obelisk.sv.type.covergroup_type @derived attributes {base_group = !obelisk.covergroup_handle<@root::@base>,
        constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 4 : i32, has_coverage_event = false,
        node_id = 2 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@derived>
    } {
    }
  }
}

// -----

module attributes {obelisk.coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.root @root attributes {node_id = 0 : i64} {
    // expected-error @below {{base_group chain must be acyclic}}
    obelisk.sv.type.covergroup_type @self attributes {base_group = !obelisk.covergroup_handle<@root::@self>,
        constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 4 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@self>
    } {
    }
  }
}

// -----

// The same typed option is legal in the IEEE 1800-2023 semantic context.
module attributes {obelisk.coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.root @root attributes {node_id = 0 : i64} {
    obelisk.sv.type.covergroup_type @g attributes {constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g>
    } {
      obelisk.sv.coverage.option attributes {node_id = 2 : i64, option_kind = 14 : i32, owner_kind = 0 : i32,
          owner_symbol = @root::@g, scope_kind = 1 : i32} {
        obelisk.sv.expression.real_literal attributes {constant_value = "0.25", node_id = 3 : i64,
            semantic_type = !obelisk.real} {
        }
      }
    }
  }
}

// -----

module attributes {obelisk.coverage.language_version = 2017 : i32} {
  obelisk.sv.symbol.root @root attributes {node_id = 0 : i64} {
    obelisk.sv.type.covergroup_type @g attributes {constructor_argument_count = 0 : i64, constructor_formals = [],
        coverage_event_kind = 0 : i32, has_coverage_event = false,
        node_id = 1 : i64, sample_formal_count = 0 : i64,
        sample_formals = [], semantic_type = !obelisk.covergroup_handle<@root::@g>
    } {
      // expected-error @below {{coverage option requires IEEE 1800-2023 or later}}
      obelisk.sv.coverage.option attributes {node_id = 2 : i64, option_kind = 14 : i32, owner_kind = 0 : i32,
          owner_symbol = @root::@g, scope_kind = 1 : i32} {
        obelisk.sv.expression.real_literal attributes {constant_value = "0.25", node_id = 3 : i64,
            semantic_type = !obelisk.real} {
        }
      }
    }
  }
}
