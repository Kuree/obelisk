// RUN: talos-opt %s '--lower-talos-to-sim=vpi=read' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: env TALOS_TEST_INPUT=%t.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SourceValueDefaults

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 0 : i64} {
    talos.sv.symbol.compilation_unit @cu attributes {hierarchical_name = "$unit", node_id = 100 : i64} {
      talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 101 : i64,
          is_interface = false, specialization_count = 0 : i64} {
talos.sv.source.value_parameter @n attributes {node_id = 3 : i64, hierarchical_name = "G::N", name = "N"} {
talos.sv.source.value_default attributes {node_id = 2 : i64} {
talos.sv.expression.integer_literal attributes {node_id = 1 : i64, constant_value = "4", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
}
}
talos.sv.source.type_parameter @t attributes {node_id = 4 : i64, hierarchical_name = "G::T", name = "T"} { talos.sv.source.type_default attributes {node_id = 5 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {} }
talos.sv.source.value_parameter @m attributes {node_id = 10 : i64, hierarchical_name = "G::M", name = "M"} {
talos.sv.source.value_default attributes {node_id = 9 : i64} {
talos.sv.source.binary_expression attributes {node_id = 8 : i64, operator_kind = 0 : i32} {
talos.sv.source.parameter_reference attributes {node_id = 6 : i64, parameter = @n} {}
talos.sv.expression.integer_literal attributes {node_id = 7 : i64, constant_value = "2", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
}
}
}
      }
      talos.sv.symbol.generic_class_def @h attributes {hierarchical_name = "H", name = "H", node_id = 199 : i64,
          is_interface = false, specialization_count = 0 : i64} {
        talos.sv.source.value_parameter @n attributes {node_id = 227 : i64, hierarchical_name = "H::N", name = "N"} {
        talos.sv.source.value_default attributes {node_id = 228 : i64} {
        talos.sv.expression.integer_literal attributes {node_id = 201 : i64, constant_value = "1", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        }
        }
        talos.sv.source.value_parameter @simple attributes {node_id = 229 : i64, hierarchical_name = "H::Simple", name = "Simple"} {
        talos.sv.source.value_default attributes {node_id = 230 : i64} {
        talos.sv.source.conditional_expression attributes {node_id = 205 : i64} {
        talos.sv.source.parameter_reference attributes {node_id = 202 : i64, parameter = @n} {
        }
        talos.sv.expression.integer_literal attributes {node_id = 203 : i64, constant_value = "2", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.expression.integer_literal attributes {node_id = 204 : i64, constant_value = "3", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        }
        }
        }
        talos.sv.source.value_parameter @condition attributes {node_id = 231 : i64, hierarchical_name = "H::Condition", name = "Condition"} {
        talos.sv.source.value_default attributes {node_id = 232 : i64} {
        talos.sv.source.conditional_expression attributes {node_id = 212 : i64} {
        talos.sv.source.conditional_expression attributes {node_id = 209 : i64} {
        talos.sv.source.parameter_reference attributes {node_id = 206 : i64, parameter = @n} {
        }
        talos.sv.expression.integer_literal attributes {node_id = 207 : i64, constant_value = "4", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.expression.integer_literal attributes {node_id = 208 : i64, constant_value = "5", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        }
        talos.sv.expression.integer_literal attributes {node_id = 210 : i64, constant_value = "6", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.expression.integer_literal attributes {node_id = 211 : i64, constant_value = "7", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        }
        }
        }
        talos.sv.source.value_parameter @left attributes {node_id = 233 : i64, hierarchical_name = "H::Left", name = "Left"} {
        talos.sv.source.value_default attributes {node_id = 234 : i64} {
        talos.sv.source.conditional_expression attributes {node_id = 219 : i64} {
        talos.sv.source.parameter_reference attributes {node_id = 213 : i64, parameter = @n} {
        }
        talos.sv.source.conditional_expression attributes {node_id = 217 : i64} {
        talos.sv.source.parameter_reference attributes {node_id = 214 : i64, parameter = @n} {
        }
        talos.sv.expression.integer_literal attributes {node_id = 215 : i64, constant_value = "8", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.expression.integer_literal attributes {node_id = 216 : i64, constant_value = "9", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        }
        talos.sv.expression.integer_literal attributes {node_id = 218 : i64, constant_value = "10", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        }
        }
        }
        talos.sv.source.value_parameter @right attributes {node_id = 235 : i64, hierarchical_name = "H::Right", name = "Right"} {
        talos.sv.source.value_default attributes {node_id = 236 : i64} {
        talos.sv.source.conditional_expression attributes {node_id = 226 : i64} {
        talos.sv.source.parameter_reference attributes {node_id = 220 : i64, parameter = @n} {
        }
        talos.sv.expression.integer_literal attributes {node_id = 221 : i64, constant_value = "11", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.source.conditional_expression attributes {node_id = 225 : i64} {
        talos.sv.source.parameter_reference attributes {node_id = 222 : i64, parameter = @n} {
        }
        talos.sv.expression.integer_literal attributes {node_id = 223 : i64, constant_value = "12", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.expression.integer_literal attributes {node_id = 224 : i64, constant_value = "13", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        }
        }
        }
        }
      }
      talos.sv.type.class_type @c attributes {bitstream_width = 0 : i64,
          declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [],
          has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C",
          implemented_interfaces = [], is_abstract = false, is_final = false,
          is_interface = false, is_uninstantiated = false, name = "C", node_id = 102 : i64,
          semantic_type = !talos.class_handle<@root::@cu::@c>} {
talos.sv.symbol.parameter @p attributes {node_id = 13 : i64, hierarchical_name = "C::P", name = "P", constant_value = "9", semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
talos.sv.source.value_default attributes {node_id = 12 : i64} {
talos.sv.expression.integer_literal attributes {node_id = 11 : i64, constant_value = "4", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
}
}
talos.sv.symbol.parameter @q attributes {node_id = 18 : i64, hierarchical_name = "C::Q", name = "Q", constant_value = "11", semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
talos.sv.source.value_default attributes {node_id = 17 : i64} {
talos.sv.source.binary_expression attributes {node_id = 16 : i64, operator_kind = 0 : i32} {
talos.sv.source.parameter_reference attributes {node_id = 14 : i64, parameter = @p} {}
talos.sv.expression.integer_literal attributes {node_id = 15 : i64, constant_value = "2", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
}
}
}
talos.sv.symbol.parameter @r attributes {node_id = 25 : i64, hierarchical_name = "C::R", name = "R", constant_value = "33", semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
talos.sv.source.value_default attributes {node_id = 24 : i64} {
talos.sv.source.binary_expression attributes {node_id = 23 : i64, operator_kind = 2 : i32} {
talos.sv.source.binary_expression attributes {node_id = 21 : i64, operator_kind = 0 : i32} {
talos.sv.source.parameter_reference attributes {node_id = 19 : i64, parameter = @p} {}
talos.sv.expression.integer_literal attributes {node_id = 20 : i64, constant_value = "2", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
}
talos.sv.expression.integer_literal attributes {node_id = 22 : i64, constant_value = "3", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
}
}
}
talos.sv.symbol.parameter @s attributes {node_id = 28 : i64, hierarchical_name = "C::S", name = "S", constant_value = "32'shffffffff", semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
talos.sv.source.value_default attributes {node_id = 27 : i64} {
talos.sv.expression.integer_literal attributes {node_id = 26 : i64, constant_value = "32'shffffffff", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
}
}
talos.sv.symbol.parameter @esc attributes {node_id = 31 : i64, hierarchical_name = "C::E.p", name = "E.p", constant_value = "6", semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
talos.sv.source.value_default attributes {node_id = 30 : i64} {
talos.sv.expression.integer_literal attributes {node_id = 29 : i64, constant_value = "6", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
}
}
talos.sv.symbol.parameter @use attributes {node_id = 36 : i64, hierarchical_name = "C::UseEscaped", name = "UseEscaped", constant_value = "8", semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
talos.sv.source.value_default attributes {node_id = 35 : i64} {
talos.sv.source.binary_expression attributes {node_id = 34 : i64, operator_kind = 0 : i32} {
talos.sv.source.parameter_reference attributes {node_id = 32 : i64, parameter = @esc} {}
talos.sv.expression.integer_literal attributes {node_id = 33 : i64, constant_value = "2", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
}
}
}
talos.sv.symbol.parameter @repeated attributes {node_id = 41 : i64, hierarchical_name = "C::Repeated", name = "Repeated", constant_value = "18", semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
talos.sv.source.value_default attributes {node_id = 40 : i64} {
talos.sv.source.binary_expression attributes {node_id = 39 : i64, operator_kind = 0 : i32} {
talos.sv.source.parameter_reference attributes {node_id = 37 : i64, parameter = @p} {}
talos.sv.source.parameter_reference attributes {node_id = 38 : i64, parameter = @p} {}
}
}
}
      }
    }
  }
}

// CHECK-DAG: simulation.vpi_object.anchor @[[G:[^ ]+]] {{.*}}type 652 {{.*}}hierarchy "G"
// CHECK-DAG: simulation.vpi_object.anchor @[[N:[^ ]+]] {{.*}}type 41 {{.*}}parent @[[G]] ordinal 0 hierarchy "G::N"{{.*}}unresolved_value_parameter
// CHECK-DAG: simulation.vpi_object.anchor @[[T:[^ ]+]] {{.*}}type 609 {{.*}}parent @[[G]] ordinal 1 hierarchy "G::T"{{.*}}unresolved_type_parameter
// CHECK-DAG: simulation.vpi_object.anchor @[[M:[^ ]+]] {{.*}}type 41 {{.*}}parent @[[G]] ordinal 2 hierarchy "G::M"{{.*}}unresolved_value_parameter
// CHECK-DAG: simulation.vpi_object.anchor @[[NVAL:[^ ]+]] {{.*}}type 7 {{.*}}parent @[[N]] ordinal 0 hierarchy "" debug ""{{.*}}immutable_value
// CHECK-DAG: simulation.vpi_relation.decl <kind = symbol, symbol = @[[N]]> selector 102 handle ordinal 0 to <kind = symbol, symbol = @[[NVAL]]>
// CHECK-DAG: simulation.vpi_object.anchor @[[MADD:[^ ]+]] {{.*}}type 39 {{.*}}parent @[[M]] ordinal 0 hierarchy "" debug ""{{.*}}selector = 39 : i32, value = 24 : i32
// CHECK-DAG: simulation.vpi_relation.decl <kind = symbol, symbol = @[[MADD]]> selector 97 iterate ordinal 0 to <kind = symbol, symbol = @[[N]]>
// CHECK-DAG: simulation.vpi_object.anchor @[[MVAL:[^ ]+]] {{.*}}type 7 {{.*}}parent @[[MADD]] ordinal 0 hierarchy "" debug ""
// CHECK-DAG: simulation.vpi_relation.decl <kind = symbol, symbol = @[[MADD]]> selector 97 iterate ordinal 1 to <kind = symbol, symbol = @[[MVAL]]>

// CHECK-DAG: simulation.vpi_object.anchor @[[COND:[^ ]+]] {{.*}}type 39 {{.*}}selector = 39 : i32, value = 32 : i32
// CHECK-DAG: simulation.vpi_relation.decl <kind = symbol, symbol = @[[COND]]> selector 97 iterate ordinal 0
// CHECK-DAG: simulation.vpi_relation.decl <kind = symbol, symbol = @[[COND]]> selector 97 iterate ordinal 1
// CHECK-DAG: simulation.vpi_relation.decl <kind = symbol, symbol = @[[COND]]> selector 97 iterate ordinal 2
