// RUN: talos-opt %s -split-input-file -verify-diagnostics

module {
    // expected-error @+1 {{source constraint requires a lexical class owner}}
    talos.sv.source.class_constraint @c attributes {hierarchical_name = "G::C", name = "C", node_id = 1 : i64, has_external_definition = false} {}
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source constraint cannot carry an effective type}}
    talos.sv.source.class_constraint @c attributes {hierarchical_name = "G::C", name = "C", node_id = 1 : i64, has_external_definition = false, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source constraint requires a nonempty declaration name}}
    talos.sv.source.class_constraint @c attributes {hierarchical_name = "G::C", name = "", node_id = 1 : i64, has_external_definition = false} {}
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source constraint requires a nonempty declaration name}}
    talos.sv.source.class_constraint @c attributes {hierarchical_name = "G::C", name = "C\00", node_id = 1 : i64, has_external_definition = false} {}
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source constraint cannot contain a bound body}}
    talos.sv.source.class_constraint @c attributes {hierarchical_name = "G::C", name = "C", node_id = 1 : i64, has_external_definition = false} {
      talos.sv.expression.integer_literal attributes {node_id = 2 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, constant_value = "1"} {}
    }
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{pure constraint requires a bodyless prototype}}
    talos.sv.source.class_constraint @c attributes {hierarchical_name = "G::C", name = "C", node_id = 1 : i64, has_external_definition = false, is_pure} {}
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{pure constraint requires a bodyless prototype}}
    talos.sv.source.class_constraint @c attributes {hierarchical_name = "G::C", name = "C", node_id = 1 : i64, has_external_definition = true, is_pure, is_prototype} {}
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{external constraint definition requires a prototype}}
    talos.sv.source.class_constraint @c attributes {hierarchical_name = "G::C", name = "C", node_id = 1 : i64, has_external_definition = true} {}
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    talos.sv.source.type_parameter @t attributes {hierarchical_name = "G::C", name = "C", node_id = 2 : i64} {}
    // expected-error @+1 {{duplicates a lexical class declaration name}}
    talos.sv.source.class_constraint @c attributes {hierarchical_name = "G::C", name = "C", node_id = 1 : i64, has_external_definition = false} {}
  }
}
