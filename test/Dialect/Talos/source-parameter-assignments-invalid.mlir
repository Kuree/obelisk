// RUN: talos-opt %s --split-input-file --verify-diagnostics -o /dev/null

// ordinal gap
!int = !talos.integral<32, true, false, 31 : 0, int>
module {
 talos.sv.symbol.instance_body @owner attributes {node_id = 1 : i64, name = "parent", hierarchical_name = "parent"} {
  talos.sv.symbol.parameter @P attributes {node_id = 2 : i64, name = "P", hierarchical_name = "parent.P", semantic_type = !int, constant_value = "7"} {}
  talos.sv.symbol.instance @child attributes {node_id = 3 : i64, name = "child", hierarchical_name = "parent.child", is_uninstantiated = false} {
   talos.sv.symbol.instance_body @body attributes {node_id = 4 : i64, name = "child", hierarchical_name = "parent.child"} {
    talos.sv.symbol.parameter @N attributes {node_id = 5 : i64, name = "N", hierarchical_name = "parent.child.N", semantic_type = !int, constant_value = "7"} {}
   }
  }
  // expected-error @+1 {{parameter actual ordinals must be dense source order}}
  talos.sv.source.parameter_actual attributes {node_id = 6 : i64, parameter = @owner::@child::@body::@N, ordinal = 1 : i64} {
   talos.sv.source.parameter_reference attributes {node_id = 7 : i64, parameter = @P} {}
  }
 }
}

// -----

// ordinal overflow
!int = !talos.integral<32, true, false, 31 : 0, int>
module {
 talos.sv.symbol.instance_body @owner attributes {node_id = 1 : i64, name = "parent", hierarchical_name = "parent"} {
  talos.sv.symbol.parameter @P attributes {node_id = 2 : i64, name = "P", hierarchical_name = "parent.P", semantic_type = !int, constant_value = "7"} {}
  talos.sv.symbol.instance @child attributes {node_id = 3 : i64, name = "child", hierarchical_name = "parent.child", is_uninstantiated = false} {
   talos.sv.symbol.instance_body @body attributes {node_id = 4 : i64, name = "child", hierarchical_name = "parent.child"} {
    talos.sv.symbol.parameter @N attributes {node_id = 5 : i64, name = "N", hierarchical_name = "parent.child.N", semantic_type = !int, constant_value = "7"} {}
   }
  }
  // expected-error @+1 {{parameter actual ordinal exceeds the compact range}}
  talos.sv.source.parameter_actual attributes {node_id = 6 : i64, parameter = @owner::@child::@body::@N, ordinal = 4294967296 : i64} {
   talos.sv.source.parameter_reference attributes {node_id = 7 : i64, parameter = @P} {}
  }
 }
}

// -----

// own parameter target
!int = !talos.integral<32, true, false, 31 : 0, int>
module {
 talos.sv.symbol.instance_body @owner attributes {node_id = 1 : i64, name = "parent", hierarchical_name = "parent"} {
  talos.sv.symbol.parameter @P attributes {node_id = 2 : i64, name = "P", hierarchical_name = "parent.P", semantic_type = !int, constant_value = "7"} {}
  talos.sv.symbol.instance @child attributes {node_id = 3 : i64, name = "child", hierarchical_name = "parent.child", is_uninstantiated = false} {
   talos.sv.symbol.instance_body @body attributes {node_id = 4 : i64, name = "child", hierarchical_name = "parent.child"} {
    talos.sv.symbol.parameter @N attributes {node_id = 5 : i64, name = "N", hierarchical_name = "parent.child.N", semantic_type = !int, constant_value = "7"} {}
   }
  }
  // expected-error @+1 {{parameter actual target must belong to a direct child instance}}
  talos.sv.source.parameter_actual attributes {node_id = 6 : i64, parameter = @owner::@P, ordinal = 0 : i64} {
   talos.sv.source.parameter_reference attributes {node_id = 7 : i64, parameter = @P} {}
  }
 }
}

// -----

// wrong target kind
!int = !talos.integral<32, true, false, 31 : 0, int>
module {
 talos.sv.symbol.instance_body @owner attributes {node_id = 1 : i64, name = "parent", hierarchical_name = "parent"} {
  talos.sv.symbol.parameter @P attributes {node_id = 2 : i64, name = "P", hierarchical_name = "parent.P", semantic_type = !int, constant_value = "7"} {}
  talos.sv.symbol.instance @child attributes {node_id = 3 : i64, name = "child", hierarchical_name = "parent.child", is_uninstantiated = false} {
   talos.sv.symbol.instance_body @body attributes {node_id = 4 : i64, name = "child", hierarchical_name = "parent.child"} {
    talos.sv.symbol.type_parameter @N attributes {node_id = 5 : i64, name = "N", hierarchical_name = "parent.child.N", semantic_type = !int, constant_value = "7"} {}
   }
  }
  // expected-error @+1 {{parameter actual RHS and target kinds disagree}}
  talos.sv.source.parameter_actual attributes {node_id = 6 : i64, parameter = @owner::@child::@body::@N, ordinal = 0 : i64} {
   talos.sv.source.parameter_reference attributes {node_id = 7 : i64, parameter = @P} {}
  }
 }
}

// -----

// foreign RHS parameter
!int = !talos.integral<32, true, false, 31 : 0, int>
module {
 talos.sv.symbol.instance_body @owner attributes {node_id = 1 : i64, name = "parent", hierarchical_name = "parent"} {
  talos.sv.symbol.parameter @P attributes {node_id = 2 : i64, name = "P", hierarchical_name = "parent.P", semantic_type = !int, constant_value = "7"} {}
  talos.sv.symbol.instance @child attributes {node_id = 3 : i64, name = "child", hierarchical_name = "parent.child", is_uninstantiated = false} {
   talos.sv.symbol.instance_body @body attributes {node_id = 4 : i64, name = "child", hierarchical_name = "parent.child"} {
    talos.sv.symbol.parameter @N attributes {node_id = 5 : i64, name = "N", hierarchical_name = "parent.child.N", semantic_type = !int, constant_value = "7"} {}
   }
  }
  talos.sv.source.parameter_actual attributes {node_id = 6 : i64, parameter = @owner::@child::@body::@N, ordinal = 0 : i64} {
  // expected-error @+1 {{parameter actual reference requires an exact parameter in the instantiating body}}
   talos.sv.source.parameter_reference attributes {node_id = 7 : i64, parameter = @N} {}
  }
 }
}

// -----

// unavailable with subtree
!int = !talos.integral<32, true, false, 31 : 0, int>
module {
 talos.sv.symbol.instance_body @owner attributes {node_id = 1 : i64, name = "parent", hierarchical_name = "parent"} {
  talos.sv.symbol.parameter @P attributes {node_id = 2 : i64, name = "P", hierarchical_name = "parent.P", semantic_type = !int, constant_value = "7"} {}
  talos.sv.symbol.instance @child attributes {node_id = 3 : i64, name = "child", hierarchical_name = "parent.child", is_uninstantiated = false} {
   talos.sv.symbol.instance_body @body attributes {node_id = 4 : i64, name = "child", hierarchical_name = "parent.child"} {
    talos.sv.symbol.parameter @N attributes {node_id = 5 : i64, name = "N", hierarchical_name = "parent.child.N", semantic_type = !int, constant_value = "7"} {}
   }
  }
  // expected-error @+1 {{parameter actual requires one original RHS, named empty syntax, or explicit unavailability}}
  talos.sv.source.parameter_actual attributes {node_id = 6 : i64, parameter = @owner::@child::@body::@N, ordinal = 0 : i64, unavailable_rhs = "P"} {
   talos.sv.source.parameter_reference attributes {node_id = 7 : i64, parameter = @P} {}
  }
 }
}

// -----

// empty positional
!int = !talos.integral<32, true, false, 31 : 0, int>
module {
 talos.sv.symbol.instance_body @owner attributes {node_id = 1 : i64, name = "parent", hierarchical_name = "parent"} {
  talos.sv.symbol.parameter @P attributes {node_id = 2 : i64, name = "P", hierarchical_name = "parent.P", semantic_type = !int, constant_value = "7"} {}
  talos.sv.symbol.instance @child attributes {node_id = 3 : i64, name = "child", hierarchical_name = "parent.child", is_uninstantiated = false} {
   talos.sv.symbol.instance_body @body attributes {node_id = 4 : i64, name = "child", hierarchical_name = "parent.child"} {
    talos.sv.symbol.parameter @N attributes {node_id = 5 : i64, name = "N", hierarchical_name = "parent.child.N", semantic_type = !int, constant_value = "7"} {}
   }
  }
  // expected-error @+1 {{parameter actual requires one original RHS, named empty syntax, or explicit unavailability}}
  talos.sv.source.parameter_actual attributes {node_id = 6 : i64, parameter = @owner::@child::@body::@N, ordinal = 0 : i64, use_default = unit} {
  }
 }
}

// -----

// local target
!int = !talos.integral<32, true, false, 31 : 0, int>
module {
 talos.sv.symbol.instance_body @owner attributes {node_id = 1 : i64, name = "parent", hierarchical_name = "parent"} {
  talos.sv.symbol.parameter @P attributes {node_id = 2 : i64, name = "P", hierarchical_name = "parent.P", semantic_type = !int, constant_value = "7"} {}
  talos.sv.symbol.instance @child attributes {node_id = 3 : i64, name = "child", hierarchical_name = "parent.child", is_uninstantiated = false} {
   talos.sv.symbol.instance_body @body attributes {node_id = 4 : i64, name = "child", hierarchical_name = "parent.child"} {
    talos.sv.symbol.parameter @N attributes {node_id = 5 : i64, is_local_param = unit, name = "N", hierarchical_name = "parent.child.N", semantic_type = !int, constant_value = "7"} {}
   }
  }
  // expected-error @+1 {{parameter actual must name a nonlocal parameter}}
  talos.sv.source.parameter_actual attributes {node_id = 6 : i64, parameter = @owner::@child::@body::@N, ordinal = 0 : i64} {
   talos.sv.source.parameter_reference attributes {node_id = 7 : i64, parameter = @P} {}
  }
 }
}
