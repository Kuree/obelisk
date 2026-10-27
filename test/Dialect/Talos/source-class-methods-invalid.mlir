// RUN: talos-opt %s -split-input-file -verify-diagnostics

module {
  // expected-error @+1 {{source method requires a lexical class owner}}
  talos.sv.source.class_method @f attributes {name = "f", node_id = 1 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32} {}
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source method cannot carry an effective signature}}
    talos.sv.source.class_method @f attributes {name = "f", node_id = 1 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32, semantic_type = !talos.subroutine<() -> !talos.void, false>} {}
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source method requires a declaration name}}
    talos.sv.source.class_method @f attributes {name = "", node_id = 1 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32} {}
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source method cannot contain an elaborated body}}
    talos.sv.source.class_method @f attributes {name = "f", node_id = 1 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32} {
      talos.sv.source.type_default attributes {node_id = 2 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
    }
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{constructor requires a nonstatic, nonvirtual function named new}}
    talos.sv.source.class_method @f attributes {name = "new", node_id = 1 : i64, subroutine_kind = 1 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32, is_constructor} {}
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{static method cannot be virtual or pure}}
    talos.sv.source.class_method @f attributes {name = "f", node_id = 1 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32, is_static, is_declared_virtual} {}
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{pure method must be declared virtual}}
    talos.sv.source.class_method @f attributes {name = "f", node_id = 1 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32, is_pure} {}
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    talos.sv.source.type_parameter @t attributes {name = "f", node_id = 2 : i64} {}
    // expected-error @+1 {{duplicates a lexical class declaration name}}
    talos.sv.source.class_method @f attributes {name = "f", node_id = 1 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32} {}
  }
}
