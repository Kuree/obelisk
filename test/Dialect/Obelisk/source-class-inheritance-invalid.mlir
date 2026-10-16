// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  // expected-error @+1 {{source extends requires a lexical class owner}}
  obelisk.sv.source.class_extends attributes {node_id = 0 : i64, unavailable_base = "T"} {}
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {name = "G", hierarchical_name = "G", node_id = 1 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source extends requires exactly one base declaration or unavailable source form}}
    obelisk.sv.source.class_extends attributes {node_id = 2 : i64} {}
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {name = "G", hierarchical_name = "G", node_id = 1 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{base definition must name an exact source class}}
    obelisk.sv.source.class_extends attributes {node_id = 2 : i64, base_definition = @absent} {}
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {name = "G", hierarchical_name = "G", node_id = 1 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source class inheritance contains a cycle}}
    obelisk.sv.source.class_extends attributes {node_id = 2 : i64, base_definition = @g} {}
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {name = "G", hierarchical_name = "G", node_id = 1 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source extends cannot carry an effective type or body}}
    obelisk.sv.source.class_extends attributes {node_id = 2 : i64, unavailable_base = "T", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {name = "G", hierarchical_name = "G", node_id = 1 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{unavailable source syntax must be nonempty without NUL bytes}}
    obelisk.sv.source.class_extends attributes {node_id = 2 : i64, unavailable_base = ""} {}
  }
}
