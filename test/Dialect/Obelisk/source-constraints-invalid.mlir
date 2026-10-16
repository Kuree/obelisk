// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
    // expected-error @+1 {{source constraint requires a lexical class owner}}
    obelisk.sv.source.class_constraint @c attributes {hierarchical_name = "G::C", name = "C", node_id = 1 : i64, has_external_definition = false} {}
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source constraint cannot carry an effective type}}
    obelisk.sv.source.class_constraint @c attributes {hierarchical_name = "G::C", name = "C", node_id = 1 : i64, has_external_definition = false, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source constraint requires a nonempty declaration name}}
    obelisk.sv.source.class_constraint @c attributes {hierarchical_name = "G::C", name = "", node_id = 1 : i64, has_external_definition = false} {}
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source constraint requires a nonempty declaration name}}
    obelisk.sv.source.class_constraint @c attributes {hierarchical_name = "G::C", name = "C\00", node_id = 1 : i64, has_external_definition = false} {}
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source constraint cannot contain a bound body}}
    obelisk.sv.source.class_constraint @c attributes {hierarchical_name = "G::C", name = "C", node_id = 1 : i64, has_external_definition = false} {
      obelisk.sv.expression.integer_literal attributes {node_id = 2 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, constant_value = "1"} {}
    }
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{pure constraint requires a bodyless prototype}}
    obelisk.sv.source.class_constraint @c attributes {hierarchical_name = "G::C", name = "C", node_id = 1 : i64, has_external_definition = false, is_pure} {}
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{pure constraint requires a bodyless prototype}}
    obelisk.sv.source.class_constraint @c attributes {hierarchical_name = "G::C", name = "C", node_id = 1 : i64, has_external_definition = true, is_pure, is_prototype} {}
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{external constraint definition requires a prototype}}
    obelisk.sv.source.class_constraint @c attributes {hierarchical_name = "G::C", name = "C", node_id = 1 : i64, has_external_definition = true} {}
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.type_parameter @t attributes {hierarchical_name = "G::C", name = "C", node_id = 2 : i64} {}
    // expected-error @+1 {{duplicates a lexical class declaration name}}
    obelisk.sv.source.class_constraint @c attributes {hierarchical_name = "G::C", name = "C", node_id = 1 : i64, has_external_definition = false} {}
  }
}
