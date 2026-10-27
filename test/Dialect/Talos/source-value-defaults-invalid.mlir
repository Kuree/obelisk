// RUN: talos-opt %s -split-input-file -verify-diagnostics

module {
talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
// expected-error @+1 {{source value parameter cannot carry an effective type}}
talos.sv.source.value_parameter @p attributes {name = "P", node_id = 1 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
}
}

// -----

module {
talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
talos.sv.source.value_parameter @p attributes {name = "P", node_id = 1 : i64} {
// expected-error @+1 {{unbound source default cannot carry an effective type}}
talos.sv.source.value_default attributes {node_id = 2 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
talos.sv.expression.integer_literal attributes {node_id = 9 : i64, constant_value = "1", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
}
}
}
}

// -----

module {
talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
talos.sv.source.value_parameter @p attributes {name = "P", node_id = 1 : i64} {
talos.sv.source.value_default attributes {node_id = 2 : i64} {
// expected-error @+1 {{unbound source expression cannot carry an effective type}}
talos.sv.source.unary_expression attributes {node_id = 3 : i64, operator_kind = 1 : i32, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
talos.sv.expression.integer_literal attributes {node_id = 9 : i64, constant_value = "1", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
}
}
}
}
}

// -----

module {
talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
talos.sv.source.value_parameter @p attributes {name = "P", node_id = 1 : i64} {
talos.sv.source.value_default attributes {node_id = 2 : i64} {
// expected-error @+1 {{default reference must name a preceding value parameter}}
talos.sv.source.parameter_reference attributes {node_id = 3 : i64, parameter = @p} {}
}
}
}
}

// -----

module {
talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
talos.sv.source.value_parameter @p attributes {name = "P", node_id = 1 : i64} {
talos.sv.source.value_default attributes {node_id = 2 : i64} {
// expected-error @+1 {{source expression has an incorrect operand count}}
talos.sv.source.binary_expression attributes {node_id = 3 : i64, operator_kind = 0 : i32} {
talos.sv.expression.integer_literal attributes {node_id = 9 : i64, constant_value = "1", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
}
}
}
}
}

// -----

module {
talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
talos.sv.source.value_parameter @p attributes {name = "P", node_id = 1 : i64} {
talos.sv.source.value_default attributes {node_id = 2 : i64} {
// expected-error @+1 {{original default operator has no VPI expression encoding}}
talos.sv.source.binary_expression attributes {node_id = 3 : i64, operator_kind = 22 : i32} {
talos.sv.expression.integer_literal attributes {node_id = 9 : i64, constant_value = "1", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
talos.sv.expression.integer_literal attributes {node_id = 9 : i64, constant_value = "1", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
}
}
}
}
}

// -----

module {
talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
talos.sv.source.type_parameter @t attributes {name = "P", node_id = 2 : i64} {}
// expected-error @+1 {{duplicates a lexical parameter declaration name}}
talos.sv.source.value_parameter @p attributes {name = "P", node_id = 1 : i64} {

}
}
}

// -----

module {
talos.sv.symbol.parameter @p attributes {name = "P", node_id = 1 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, constant_value = "1"} {
// expected-error @+1 {{declaration requires at most one original default}}
talos.sv.source.value_default attributes {node_id = 2 : i64} {
talos.sv.expression.integer_literal attributes {node_id = 9 : i64, constant_value = "1", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
}
talos.sv.source.unsupported_default attributes {node_id = 4 : i64, syntax = "foo()", reason = "call"} {}
}
}

// -----

module {
talos.sv.symbol.type_parameter @p attributes {name = "P", node_id = 1 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
// expected-error @+1 {{declaration requires at most one original default}}
talos.sv.source.type_default attributes {node_id = 2 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
talos.sv.source.unsupported_default attributes {node_id = 4 : i64, syntax = "T[1:0]", reason = "dependent"} {}
}
}

// -----

module {
talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
talos.sv.source.value_parameter @p attributes {name = "P", node_id = 1 : i64} {
talos.sv.source.value_default attributes {node_id = 2 : i64} {
// expected-error @+1 {{source expression has an incorrect operand count}}
talos.sv.source.conditional_expression attributes {node_id = 3 : i64} {
        talos.sv.expression.integer_literal attributes {node_id = 237 : i64, constant_value = "0", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.expression.integer_literal attributes {node_id = 238 : i64, constant_value = "1", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
}
}
}
}
}

// -----

module {
talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 0 : i64, is_interface = false, specialization_count = 0 : i64} {
talos.sv.source.value_parameter @p attributes {name = "P", node_id = 1 : i64} {
talos.sv.source.value_default attributes {node_id = 2 : i64} {
// expected-error @+1 {{unbound source expression cannot carry an effective type}}
talos.sv.source.conditional_expression attributes {node_id = 3 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        talos.sv.expression.integer_literal attributes {node_id = 239 : i64, constant_value = "0", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.expression.integer_literal attributes {node_id = 240 : i64, constant_value = "1", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.expression.integer_literal attributes {node_id = 241 : i64, constant_value = "2", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
}
}
}
}
}
