// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  // expected-error @+1 {{source method requires a lexical class owner}}
  obelisk.sv.source.class_method @f attributes {name = "f", node_id = 1 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32} {}
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source method cannot carry an effective signature}}
    obelisk.sv.source.class_method @f attributes {name = "f", node_id = 1 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>} {}
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source method requires a declaration name}}
    obelisk.sv.source.class_method @f attributes {name = "", node_id = 1 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32} {}
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source method cannot contain an elaborated body}}
    obelisk.sv.source.class_method @f attributes {name = "f", node_id = 1 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32} {
      obelisk.sv.source.type_default attributes {node_id = 2 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
    }
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{constructor requires a nonstatic, nonvirtual function named new}}
    obelisk.sv.source.class_method @f attributes {name = "new", node_id = 1 : i64, subroutine_kind = 1 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32, is_constructor} {}
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{static method cannot be virtual or pure}}
    obelisk.sv.source.class_method @f attributes {name = "f", node_id = 1 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32, is_static, is_declared_virtual} {}
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{pure method must be declared virtual}}
    obelisk.sv.source.class_method @f attributes {name = "f", node_id = 1 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32, is_pure} {}
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.type_parameter @t attributes {name = "f", node_id = 2 : i64} {}
    // expected-error @+1 {{duplicates a lexical class declaration name}}
    obelisk.sv.source.class_method @f attributes {name = "f", node_id = 1 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32} {}
  }
}
