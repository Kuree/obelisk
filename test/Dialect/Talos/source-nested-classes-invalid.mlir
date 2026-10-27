// RUN: talos-opt %s --split-input-file --verify-diagnostics

module {
  // expected-error @+1 {{source class requires a lexical class owner}}
  talos.sv.source.class_definition @c attributes {node_id = 19 : i64, name = "C", hierarchical_name = "C", is_abstract = false, is_interface = false, is_final = false, is_parameterized = false} {

  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {node_id = 21 : i64, hierarchical_name = "G", name = "G", is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source class cannot carry an effective type}}
    talos.sv.source.class_definition @c attributes {node_id = 20 : i64, name = "C", hierarchical_name = "G::C", semantic_type = !talos.integral<32, true, false, 31 : 0, int>, is_abstract = false, is_interface = false, is_final = false, is_parameterized = false} {

    }
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {node_id = 23 : i64, hierarchical_name = "G", name = "G", is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source class requires a nonempty declaration name}}
    talos.sv.source.class_definition @c attributes {node_id = 22 : i64, name = "", hierarchical_name = "G::C", is_abstract = false, is_interface = false, is_final = false, is_parameterized = false} {

    }
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {node_id = 25 : i64, hierarchical_name = "G", name = "G", is_interface = false, specialization_count = 0 : i64} {
    // expected-error @+1 {{source class contains a non-source declaration}}
    talos.sv.source.class_definition @c attributes {node_id = 24 : i64, name = "C", hierarchical_name = "G::C", is_abstract = false, is_interface = false, is_final = false, is_parameterized = false} {
      talos.sv.symbol.variable @v attributes {node_id = 1000 : i64} {}
    }
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {node_id = 28 : i64, hierarchical_name = "G", name = "G", is_interface = false, specialization_count = 0 : i64} {
    talos.sv.source.type_parameter @a attributes {node_id = 26 : i64, name = "C"} {}
    // expected-error @+1 {{duplicates a lexical class declaration name}}
    talos.sv.source.class_definition @b attributes {node_id = 27 : i64, name = "C", hierarchical_name = "G::C", is_abstract = false, is_interface = false, is_final = false, is_parameterized = false} {

    }
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {node_id = 32 : i64, hierarchical_name = "G", name = "G", is_interface = false, specialization_count = 1 : i64} {
    talos.sv.source.class_definition @c attributes {node_id = 29 : i64, name = "C", hierarchical_name = "G::C", is_abstract = false, is_interface = false, is_final = false, is_parameterized = false} {

    }
    talos.sv.type.class_type @outer attributes {node_id = 31 : i64, name = "G", hierarchical_name = "G#(int)", semantic_type = !talos.class_handle<@g::@outer>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false} {
      // expected-error @+1 {{lexical definition must name a source class}}
      talos.sv.type.class_type @actual attributes {node_id = 30 : i64, name = "C", hierarchical_name = "G#(int)::C", semantic_type = !talos.class_handle<@g::@outer::@actual>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, lexical_definition = @g::@missing} {

      }
    }
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {node_id = 36 : i64, hierarchical_name = "G", name = "G", is_interface = false, specialization_count = 1 : i64} {
    talos.sv.source.class_definition @c attributes {node_id = 33 : i64, name = "D", hierarchical_name = "G::D", is_abstract = false, is_interface = false, is_final = false, is_parameterized = false} {

    }
    talos.sv.type.class_type @outer attributes {node_id = 35 : i64, name = "G", hierarchical_name = "G#(int)", semantic_type = !talos.class_handle<@g::@outer>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false} {
      // expected-error @+1 {{lexical definition disagrees with class identity}}
      talos.sv.type.class_type @actual attributes {node_id = 34 : i64, name = "C", hierarchical_name = "G#(int)::C", semantic_type = !talos.class_handle<@g::@outer::@actual>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, lexical_definition = @g::@c} {

      }
    }
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @g attributes {node_id = 39 : i64, hierarchical_name = "G", name = "G", is_interface = false, specialization_count = 1 : i64} {
    talos.sv.source.class_definition @c attributes {node_id = 29 : i64, name = "C", hierarchical_name = "G::C", is_abstract = false, is_interface = false, is_final = false, is_parameterized = false} {

    }
    talos.sv.type.class_type @outer attributes {node_id = 38 : i64, name = "G", hierarchical_name = "G#(int)", semantic_type = !talos.class_handle<@g::@outer>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false} {
      // expected-error @+1 {{lexical definition disagrees with class modifiers}}
      talos.sv.type.class_type @actual attributes {node_id = 37 : i64, name = "C", hierarchical_name = "G#(int)::C", semantic_type = !talos.class_handle<@g::@outer::@actual>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, implemented_interfaces = [], is_abstract = true, is_final = false, is_interface = false, is_uninstantiated = false, lexical_definition = @g::@c} {

      }
    }
  }
}

// -----

module {
  talos.sv.symbol.generic_class_def @other attributes {node_id = 41 : i64, hierarchical_name = "Other", name = "Other", is_interface = false, specialization_count = 0 : i64} {
    talos.sv.source.class_definition @c attributes {node_id = 40 : i64, name = "C", hierarchical_name = "Other::C", is_abstract = false, is_interface = false, is_final = false, is_parameterized = false} {

    }
  }
  talos.sv.symbol.generic_class_def @g attributes {node_id = 44 : i64, hierarchical_name = "G", name = "G", is_interface = false, specialization_count = 1 : i64} {
    talos.sv.source.class_definition @c attributes {node_id = 29 : i64, name = "C", hierarchical_name = "G::C", is_abstract = false, is_interface = false, is_final = false, is_parameterized = false} {

    }
    talos.sv.type.class_type @outer attributes {node_id = 43 : i64, name = "G", hierarchical_name = "G#(int)", semantic_type = !talos.class_handle<@g::@outer>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false} {
      // expected-error @+1 {{lexical definition has a different enclosing class}}
      talos.sv.type.class_type @actual attributes {node_id = 42 : i64, name = "C", hierarchical_name = "G#(int)::C", semantic_type = !talos.class_handle<@g::@outer::@actual>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, lexical_definition = @other::@c} {

      }
    }
  }
}

// -----
module {
  talos.sv.symbol.generic_class_def @g attributes {node_id = 0 : i64, name = "G", is_interface = false, specialization_count = 1 : i64} {
    talos.sv.source.class_definition @c attributes {node_id = 1 : i64, name = "C", is_abstract = false, is_interface = false, is_final = false, is_parameterized = true} {}
    talos.sv.type.class_type @outer attributes {node_id = 2 : i64, name = "G", semantic_type = !talos.class_handle<@g::@outer>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false} {
      // expected-error @+1 {{realized generic class contains a duplicate source inventory}}
      talos.sv.symbol.generic_class_def @actual attributes {node_id = 3 : i64, name = "C", is_interface = false, specialization_count = 0 : i64, lexical_definition = @g::@c} {
        talos.sv.source.type_parameter @u attributes {node_id = 4 : i64, name = "U"} {}
      }
    }
  }
}
