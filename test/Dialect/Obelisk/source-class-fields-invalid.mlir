// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  // expected-error @+1 {{source declaration requires a lexical class owner}}
  obelisk.sv.source.class_property @x attributes {name = "x", node_id = 1 : i64, lifetime = 0 : i32, member_visibility = 0 : i32, rand_mode = 0 : i32} { obelisk.sv.source.declared_type attributes {node_id = 2 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {} }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source declaration requires exactly one declared-type node}}
    obelisk.sv.source.class_property @x attributes {name = "x", node_id = 1 : i64, lifetime = 0 : i32, member_visibility = 0 : i32, rand_mode = 0 : i32} {}
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source declaration cannot carry an effective payload}}
    obelisk.sv.source.class_property @x attributes {name = "x", node_id = 1 : i64, lifetime = 0 : i32, member_visibility = 0 : i32, rand_mode = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} { obelisk.sv.source.declared_type attributes {node_id = 2 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {} }
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.class_property @x attributes {name = "x", node_id = 1 : i64, lifetime = 0 : i32, member_visibility = 0 : i32, rand_mode = 0 : i32} {
      // expected-error @+1 {{declared type reference must name a lexical type in the same generic class}}
      obelisk.sv.source.declared_type_reference attributes {node_id = 2 : i64, declaration = @missing} {}
    }
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.class_property @x attributes {name = "x", node_id = 1 : i64, lifetime = 0 : i32, member_visibility = 0 : i32, rand_mode = 0 : i32} {
      // expected-error @+1 {{declared type reference must name a preceding lexical type}}
      obelisk.sv.source.declared_type_reference attributes {node_id = 2 : i64, declaration = @t} {}
    }
    obelisk.sv.source.type_parameter @t attributes {name = "T", node_id = 3 : i64} {}
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.class_property @x attributes {name = "x", node_id = 1 : i64, lifetime = 0 : i32, member_visibility = 0 : i32, rand_mode = 0 : i32} {
      // expected-error @+1 {{unbound declared type cannot carry an effective type}}
      obelisk.sv.source.unavailable_type attributes {node_id = 2 : i64, syntax = "T[N]", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
    }
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source virtuality conflicts with declaration modifiers}}
    obelisk.sv.source.class_method @f attributes {name = "f", node_id = 1 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32, is_static, source_virtuality = true} {}
  }
}
