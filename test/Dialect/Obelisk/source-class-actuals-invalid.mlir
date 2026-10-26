// RUN: obelisk-opt %s -split-input-file -verify-diagnostics

module {
  obelisk.sv.symbol.generic_class_def @base attributes {node_id = 1 : i64, hierarchical_name = "Base", name = "Base", is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.type_parameter @t attributes {node_id = 2 : i64, hierarchical_name = "Base::T", name = "T"} {}
    obelisk.sv.source.value_parameter @p attributes {node_id = 3 : i64, hierarchical_name = "Base::P", name = "P"} {}
    obelisk.sv.source.value_parameter @l attributes {node_id = 4 : i64, hierarchical_name = "Base::L", name = "L", is_local_param} {}
  }
  obelisk.sv.symbol.generic_class_def @derived attributes {node_id = 5 : i64, hierarchical_name = "Derived", name = "Derived", is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.value_parameter @n attributes {node_id = 6 : i64, hierarchical_name = "Derived::N", name = "N"} {}
    obelisk.sv.source.value_parameter @l attributes {node_id = 7 : i64, hierarchical_name = "Derived::L", name = "L", is_local_param} {}
    obelisk.sv.source.class_extends attributes {node_id = 8 : i64, base_definition = @base} {
      // expected-error @+1 {{source connection requires one typed source expression}}
      obelisk.sv.source.class_parameter_actual attributes {node_id = 9 : i64, parameter = @base::@p} {

      }
    }
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @base attributes {node_id = 1 : i64, hierarchical_name = "Base", name = "Base", is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.type_parameter @t attributes {node_id = 2 : i64, hierarchical_name = "Base::T", name = "T"} {}
    obelisk.sv.source.value_parameter @p attributes {node_id = 3 : i64, hierarchical_name = "Base::P", name = "P"} {}
    obelisk.sv.source.value_parameter @l attributes {node_id = 4 : i64, hierarchical_name = "Base::L", name = "L", is_local_param} {}
  }
  obelisk.sv.symbol.generic_class_def @derived attributes {node_id = 5 : i64, hierarchical_name = "Derived", name = "Derived", is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.value_parameter @n attributes {node_id = 6 : i64, hierarchical_name = "Derived::N", name = "N"} {}
    obelisk.sv.source.value_parameter @l attributes {node_id = 7 : i64, hierarchical_name = "Derived::L", name = "L", is_local_param} {}
    obelisk.sv.source.class_extends attributes {node_id = 8 : i64, base_definition = @base} {
      // expected-error @+1 {{explicit default connection requires named syntax and no unavailable RHS}}
      obelisk.sv.source.class_parameter_actual attributes {node_id = 9 : i64, parameter = @base::@p, use_default} {

      }
    }
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @base attributes {node_id = 1 : i64, hierarchical_name = "Base", name = "Base", is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.type_parameter @t attributes {node_id = 2 : i64, hierarchical_name = "Base::T", name = "T"} {}
    obelisk.sv.source.value_parameter @p attributes {node_id = 3 : i64, hierarchical_name = "Base::P", name = "P"} {}
    obelisk.sv.source.value_parameter @l attributes {node_id = 4 : i64, hierarchical_name = "Base::L", name = "L", is_local_param} {}
  }
  obelisk.sv.symbol.generic_class_def @derived attributes {node_id = 5 : i64, hierarchical_name = "Derived", name = "Derived", is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.value_parameter @n attributes {node_id = 6 : i64, hierarchical_name = "Derived::N", name = "N"} {}
    obelisk.sv.source.value_parameter @l attributes {node_id = 7 : i64, hierarchical_name = "Derived::L", name = "L", is_local_param} {}
    obelisk.sv.source.class_extends attributes {node_id = 8 : i64, base_definition = @base} {
      // expected-error @+1 {{connection must name an overridable parameter of the exact base class}}
      obelisk.sv.source.class_parameter_actual attributes {node_id = 9 : i64, parameter = @base::@l, is_named, unavailable_rhs = "f()"} {

      }
    }
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @base attributes {node_id = 1 : i64, hierarchical_name = "Base", name = "Base", is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.type_parameter @t attributes {node_id = 2 : i64, hierarchical_name = "Base::T", name = "T"} {}
    obelisk.sv.source.value_parameter @p attributes {node_id = 3 : i64, hierarchical_name = "Base::P", name = "P"} {}
    obelisk.sv.source.value_parameter @l attributes {node_id = 4 : i64, hierarchical_name = "Base::L", name = "L", is_local_param} {}
  }
  obelisk.sv.symbol.generic_class_def @derived attributes {node_id = 5 : i64, hierarchical_name = "Derived", name = "Derived", is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.value_parameter @n attributes {node_id = 6 : i64, hierarchical_name = "Derived::N", name = "N"} {}
    obelisk.sv.source.value_parameter @l attributes {node_id = 7 : i64, hierarchical_name = "Derived::L", name = "L", is_local_param} {}
    obelisk.sv.source.class_extends attributes {node_id = 8 : i64, base_definition = @base} {
      // expected-error @+1 {{connection must name an overridable parameter of the exact base class}}
      obelisk.sv.source.class_parameter_actual attributes {node_id = 9 : i64, parameter = @derived::@n, is_named, unavailable_rhs = "f()"} {

      }
    }
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @base attributes {node_id = 1 : i64, hierarchical_name = "Base", name = "Base", is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.type_parameter @t attributes {node_id = 2 : i64, hierarchical_name = "Base::T", name = "T"} {}
    obelisk.sv.source.value_parameter @p attributes {node_id = 3 : i64, hierarchical_name = "Base::P", name = "P"} {}
    obelisk.sv.source.value_parameter @l attributes {node_id = 4 : i64, hierarchical_name = "Base::L", name = "L", is_local_param} {}
  }
  obelisk.sv.symbol.generic_class_def @derived attributes {node_id = 5 : i64, hierarchical_name = "Derived", name = "Derived", is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.value_parameter @n attributes {node_id = 6 : i64, hierarchical_name = "Derived::N", name = "N"} {}
    obelisk.sv.source.value_parameter @l attributes {node_id = 7 : i64, hierarchical_name = "Derived::L", name = "L", is_local_param} {}
    obelisk.sv.source.class_extends attributes {node_id = 8 : i64, base_definition = @base} {
      // expected-error @+1 {{ordered connection must follow base parameter declaration order}}
      obelisk.sv.source.class_parameter_actual attributes {node_id = 9 : i64, parameter = @base::@p, unavailable_rhs = "f()"} {

      }
    }
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @base attributes {node_id = 1 : i64, hierarchical_name = "Base", name = "Base", is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.type_parameter @t attributes {node_id = 2 : i64, hierarchical_name = "Base::T", name = "T"} {}
    obelisk.sv.source.value_parameter @p attributes {node_id = 3 : i64, hierarchical_name = "Base::P", name = "P"} {}
    obelisk.sv.source.value_parameter @l attributes {node_id = 4 : i64, hierarchical_name = "Base::L", name = "L", is_local_param} {}
  }
  obelisk.sv.symbol.generic_class_def @derived attributes {node_id = 5 : i64, hierarchical_name = "Derived", name = "Derived", is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.value_parameter @n attributes {node_id = 6 : i64, hierarchical_name = "Derived::N", name = "N"} {}
    obelisk.sv.source.value_parameter @l attributes {node_id = 7 : i64, hierarchical_name = "Derived::L", name = "L", is_local_param} {}
    obelisk.sv.source.class_extends attributes {node_id = 8 : i64, base_definition = @base} {
      // expected-error @+1 {{connection expression must agree with the formal parameter kind}}
      obelisk.sv.source.class_parameter_actual attributes {node_id = 9 : i64, parameter = @base::@t, is_named} {
        obelisk.sv.source.parameter_reference attributes {node_id = 10 : i64, parameter = @n} {}
      }
    }
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @base attributes {node_id = 1 : i64, hierarchical_name = "Base", name = "Base", is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.type_parameter @t attributes {node_id = 2 : i64, hierarchical_name = "Base::T", name = "T"} {}
    obelisk.sv.source.value_parameter @p attributes {node_id = 3 : i64, hierarchical_name = "Base::P", name = "P"} {}
    obelisk.sv.source.value_parameter @l attributes {node_id = 4 : i64, hierarchical_name = "Base::L", name = "L", is_local_param} {}
  }
  obelisk.sv.symbol.generic_class_def @derived attributes {node_id = 5 : i64, hierarchical_name = "Derived", name = "Derived", is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.value_parameter @n attributes {node_id = 6 : i64, hierarchical_name = "Derived::N", name = "N"} {}
    obelisk.sv.source.value_parameter @l attributes {node_id = 7 : i64, hierarchical_name = "Derived::L", name = "L", is_local_param} {}
    obelisk.sv.source.class_extends attributes {node_id = 8 : i64, base_definition = @base} {
      // expected-error @+1 {{connection expression must agree with the formal parameter kind}}
      obelisk.sv.source.class_parameter_actual attributes {node_id = 9 : i64, parameter = @base::@p, is_named} {
        obelisk.sv.source.type_default attributes {node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {}
      }
    }
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @base attributes {node_id = 1 : i64, hierarchical_name = "Base", name = "Base", is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.type_parameter @t attributes {node_id = 2 : i64, hierarchical_name = "Base::T", name = "T"} {}
    obelisk.sv.source.value_parameter @p attributes {node_id = 3 : i64, hierarchical_name = "Base::P", name = "P"} {}
    obelisk.sv.source.value_parameter @l attributes {node_id = 4 : i64, hierarchical_name = "Base::L", name = "L", is_local_param} {}
  }
  obelisk.sv.symbol.generic_class_def @derived attributes {node_id = 5 : i64, hierarchical_name = "Derived", name = "Derived", is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.value_parameter @n attributes {node_id = 6 : i64, hierarchical_name = "Derived::N", name = "N"} {}
    obelisk.sv.source.value_parameter @l attributes {node_id = 7 : i64, hierarchical_name = "Derived::L", name = "L", is_local_param} {}
    // expected-error @+1 {{source extends children require exact parameter connections}}
    obelisk.sv.source.class_extends attributes {node_id = 8 : i64, base_definition = @base, parameter_arguments = "#(.P())"} {
      obelisk.sv.source.class_parameter_actual attributes {node_id = 9 : i64, parameter = @base::@p, is_named, use_default} {

      }
    }
  }
}

// -----

module {
  obelisk.sv.symbol.generic_class_def @base attributes {node_id = 1 : i64, hierarchical_name = "Base", name = "Base", is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.type_parameter @t attributes {node_id = 2 : i64, hierarchical_name = "Base::T", name = "T"} {}
    obelisk.sv.source.value_parameter @p attributes {node_id = 3 : i64, hierarchical_name = "Base::P", name = "P"} {}
    obelisk.sv.source.value_parameter @l attributes {node_id = 4 : i64, hierarchical_name = "Base::L", name = "L", is_local_param} {}
  }
  obelisk.sv.symbol.generic_class_def @derived attributes {node_id = 5 : i64, hierarchical_name = "Derived", name = "Derived", is_interface = false, specialization_count = 0 : i64} {
    obelisk.sv.source.value_parameter @n attributes {node_id = 6 : i64, hierarchical_name = "Derived::N", name = "N"} {}
    obelisk.sv.source.value_parameter @l attributes {node_id = 7 : i64, hierarchical_name = "Derived::L", name = "L", is_local_param} {}
    obelisk.sv.source.class_extends attributes {node_id = 8 : i64, base_definition = @base} {
      // expected-error @+1 {{source connection requires an unbound extends owner}}
      obelisk.sv.source.class_parameter_actual attributes {node_id = 9 : i64, parameter = @base::@p, is_named, use_default, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {

      }
    }
  }
}
