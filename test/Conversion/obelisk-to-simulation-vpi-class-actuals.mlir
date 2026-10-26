// RUN: obelisk-opt %s '--lower-obelisk-to-sim=vpi=read' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: env OBELISK_TEST_INPUT=%t.mlir %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.ClassActuals

// Original explicit RHS nodes remain independent of the base defaults and of
// every effective specialization (37.32 detail3). No class is instantiated.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @unrelated_def attributes {definition_kind = 0 : i32,
      hierarchical_name = "unrelated", name = "unrelated", node_id = 2000 : i64} {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 1000 : i64} {
    obelisk.sv.symbol.instance @unrelated_i attributes {hierarchical_name = "unrelated",
        is_uninstantiated = false, name = "unrelated", node_id = 2001 : i64,
        referenced_path = "unrelated", referenced_symbol = @unrelated_def} {
      obelisk.sv.symbol.instance_body @unrelated_b attributes {hierarchical_name = "unrelated",
          name = "unrelated", node_id = 2002 : i64,
          time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {}
    }
    obelisk.sv.symbol.compilation_unit @cu attributes {hierarchical_name = "$unit", node_id = 1001 : i64} {
      obelisk.sv.symbol.generic_class_def @base attributes {node_id = 1 : i64, hierarchical_name = "Base", name = "Base", is_interface = false, specialization_count = 0 : i64} {
        obelisk.sv.source.type_parameter @t attributes {node_id = 2 : i64, hierarchical_name = "Base::T", name = "T"} {
          obelisk.sv.source.type_default attributes {node_id = 3 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
        }
        obelisk.sv.source.value_parameter @p attributes {node_id = 4 : i64, hierarchical_name = "Base::P", name = "P"} {
          obelisk.sv.source.value_default attributes {node_id = 5 : i64} {
            obelisk.sv.expression.integer_literal attributes {node_id = 6 : i64, constant_value = "3", is_signed = true, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          }
        }
        obelisk.sv.source.value_parameter @q attributes {node_id = 7 : i64, hierarchical_name = "Base::Q", name = "Q"} {
          obelisk.sv.source.value_default attributes {node_id = 8 : i64} {
            obelisk.sv.source.binary_expression attributes {node_id = 9 : i64, operator_kind = 0 : i32} {
              obelisk.sv.source.parameter_reference attributes {node_id = 10 : i64, parameter = @p} {}
              obelisk.sv.expression.integer_literal attributes {node_id = 11 : i64, constant_value = "1", is_signed = true, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
            }
          }
        }
      }
      obelisk.sv.symbol.generic_class_def @derived attributes {node_id = 20 : i64, hierarchical_name = "Derived", name = "Derived", is_interface = false, specialization_count = 0 : i64} {
        obelisk.sv.source.type_parameter @u attributes {node_id = 21 : i64, hierarchical_name = "Derived::U", name = "U"} {}
        obelisk.sv.source.value_parameter @n attributes {node_id = 22 : i64, hierarchical_name = "Derived::N", name = "N"} {}
        obelisk.sv.source.class_extends attributes {node_id = 23 : i64, base_definition = @root::@cu::@base} {
          obelisk.sv.source.class_parameter_actual attributes {node_id = 24 : i64, parameter = @root::@cu::@base::@q, is_named, use_default} {}
          obelisk.sv.source.class_parameter_actual attributes {node_id = 25 : i64, parameter = @root::@cu::@base::@t, is_named} {
            obelisk.sv.source.type_reference_default attributes {node_id = 26 : i64, parameter = @u} {}
          }
          obelisk.sv.source.class_parameter_actual attributes {node_id = 27 : i64, parameter = @root::@cu::@base::@p, is_named} {
            obelisk.sv.source.binary_expression attributes {node_id = 28 : i64, operator_kind = 0 : i32} {
              obelisk.sv.source.parameter_reference attributes {node_id = 29 : i64, parameter = @n} {}
              obelisk.sv.expression.integer_literal attributes {node_id = 30 : i64, constant_value = "2", is_signed = true, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
            }
          }
        }
      }
      obelisk.sv.symbol.generic_class_def @ordered attributes {node_id = 40 : i64, hierarchical_name = "Ordered", name = "Ordered", is_interface = false, specialization_count = 0 : i64} {
        obelisk.sv.source.class_extends attributes {node_id = 41 : i64, base_definition = @root::@cu::@base} {
          obelisk.sv.source.class_parameter_actual attributes {node_id = 42 : i64, parameter = @root::@cu::@base::@t} {
            obelisk.sv.source.type_default attributes {node_id = 43 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {}
          }
          obelisk.sv.source.class_parameter_actual attributes {node_id = 44 : i64, parameter = @root::@cu::@base::@p} {
            obelisk.sv.expression.integer_literal attributes {node_id = 45 : i64, constant_value = "7", is_signed = true, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
          }
        }
      }
      obelisk.sv.symbol.generic_class_def @unsupported attributes {node_id = 50 : i64, hierarchical_name = "Unsupported", name = "Unsupported", is_interface = false, specialization_count = 0 : i64} {
        obelisk.sv.source.class_extends attributes {node_id = 51 : i64, base_definition = @root::@cu::@base} {
          obelisk.sv.source.class_parameter_actual attributes {node_id = 52 : i64, parameter = @root::@cu::@base::@p, is_named, unavailable_rhs = "f()"} {}
        }
      }
    }
  }
}
// CHECK-NOT: simulation.class.decl
// CHECK-DAG: simulation.vpi_class_parameter_assign.decl @{{[^ ]+}} {{.*}}ordinal 0 {is_named, rhs = @{{[^ ]+}}}
// CHECK-DAG: simulation.vpi_class_parameter_assign.decl @{{[^ ]+}} {{.*}}ordinal 1 {is_named, rhs = @{{[^ ]+}}}
// CHECK-DAG: simulation.vpi_class_parameter_assign.decl @{{[^ ]+}} {{.*}}ordinal 2 {is_named}
// CHECK-DAG: simulation.vpi_class_parameter_assign.decl @{{[^ ]+}} {{.*}}ordinal 1 {is_named, unavailable_rhs = "f()"}
// CHECK-DAG: simulation.vpi_typespec.decl @__obelisk_vpi_source_actual_{{[0-9]+}} {{.*}}origin = 5 : i32
