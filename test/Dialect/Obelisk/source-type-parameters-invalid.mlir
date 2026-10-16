// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source type parameter cannot carry an effective type}}
    obelisk.sv.source.type_parameter @t attributes {name = "T", node_id = 1 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.type_parameter @t attributes {name = "T", node_id = 1 : i64} {
      // expected-error @+1 {{default reference must name a preceding source type parameter}}
      obelisk.sv.source.type_reference_default attributes {node_id = 2 : i64, parameter = @t} {}
    }
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.type_parameter @t attributes {name = "T", node_id = 1 : i64} {}
    // expected-error @+1 {{duplicates a lexical type parameter declaration name}}
    obelisk.sv.source.type_parameter @other attributes {name = "T", node_id = 2 : i64} {}
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.type_parameter @t attributes {name = "T", node_id = 1 : i64} {
      // expected-error @+1 {{concrete source default cannot contain an unresolved type}}
      obelisk.sv.source.type_default attributes {node_id = 2 : i64, semantic_type = !obelisk.untyped} {}
    }
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.type_parameter @t attributes {name = "T", node_id = 1 : i64} {
      // expected-error @+1 {{unsupported default requires source syntax and a reason}}
      obelisk.sv.source.unsupported_default attributes {node_id = 2 : i64, syntax = "", reason = "dependent type"} {}
    }
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.type_parameter @t attributes {name = "T", node_id = 1 : i64} {
      // expected-error @+1 {{concrete source default cannot contain an unresolved type}}
      obelisk.sv.source.type_default attributes {node_id = 2 : i64, semantic_type = !obelisk.type_reference} {}
    }
  }
}
