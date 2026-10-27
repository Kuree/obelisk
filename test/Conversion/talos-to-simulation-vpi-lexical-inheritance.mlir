// RUN: talos-opt %s '--lower-talos-to-sim=vpi=read' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: env TALOS_TEST_INPUT=%t.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.LexicalInheritance

// LRM 37.31/37.32: zero-specialization source inheritance retains exact
// declaration identities and original defaults, including shared RHS objects.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 1000 : i64} {
    talos.sv.symbol.compilation_unit @cu attributes {hierarchical_name = "$unit", node_id = 1001 : i64} {
      talos.sv.symbol.generic_class_def @base attributes {node_id = 10 : i64, hierarchical_name = "Base", name = "Base", is_interface = false, specialization_count = 0 : i64} {
        talos.sv.source.type_parameter @t attributes {node_id = 1 : i64, hierarchical_name = "Base::T", name = "T"} {
          talos.sv.source.type_default attributes {node_id = 2 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
        }
        talos.sv.source.value_parameter @p attributes {node_id = 3 : i64, hierarchical_name = "Base::P", name = "P"} {
          talos.sv.source.value_default attributes {node_id = 4 : i64} {
            talos.sv.expression.integer_literal attributes {node_id = 5 : i64, constant_value = "3", is_signed = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
          }
        }
        talos.sv.source.value_parameter @local attributes {node_id = 6 : i64, hierarchical_name = "Base::Local", name = "Local", is_local_param} {}
        talos.sv.source.value_parameter @unsupported attributes {node_id = 7 : i64, hierarchical_name = "Base::Unsupported", name = "Unsupported"} {
          talos.sv.source.unsupported_default attributes {node_id = 8 : i64, syntax = "f()", reason = "source call is unavailable"} {}
        }
        talos.sv.source.type_parameter @required attributes {node_id = 9 : i64, hierarchical_name = "Base::Required", name = "Required"} {}
      }
      talos.sv.symbol.generic_class_def @mid attributes {node_id = 12 : i64, hierarchical_name = "Mid", name = "Mid", is_interface = false, specialization_count = 0 : i64} {
        talos.sv.source.class_extends attributes {node_id = 11 : i64, base_definition = @root::@cu::@base} {}
      }
      talos.sv.symbol.generic_class_def @derived attributes {node_id = 14 : i64, hierarchical_name = "Derived", name = "Derived", is_interface = false, specialization_count = 0 : i64} {
        talos.sv.source.class_extends attributes {node_id = 13 : i64, base_definition = @root::@cu::@mid} {}
      }
      talos.sv.symbol.generic_class_def @explicit attributes {node_id = 16 : i64, hierarchical_name = "Explicit", name = "Explicit", is_interface = false, specialization_count = 0 : i64} {
        talos.sv.source.class_extends attributes {node_id = 15 : i64, base_definition = @root::@cu::@base, parameter_arguments = "#(.P(7))"} {}
      }
      talos.sv.symbol.generic_class_def @unknown attributes {node_id = 18 : i64, hierarchical_name = "Unknown", name = "Unknown", is_interface = false, specialization_count = 0 : i64} {
        talos.sv.source.class_extends attributes {node_id = 17 : i64, unavailable_base = "T::base"} {}
      }
      talos.sv.symbol.generic_class_def @ctor attributes {node_id = 20 : i64, hierarchical_name = "Constructor", name = "Constructor", is_interface = false, specialization_count = 0 : i64} {
        talos.sv.source.class_extends attributes {node_id = 19 : i64, base_definition = @root::@cu::@base, constructor_arguments = "(P + 1)"} {}
      }
    }
  }
}
// CHECK-NOT: simulation.class.decl
// CHECK-DAG: simulation.vpi_object.anchor @[[BASE:[^ ]+]] {{.*}}type 652 {{.*}}hierarchy "Base"
// CHECK-DAG: simulation.vpi_object.anchor @[[MID:[^ ]+]] {{.*}}type 652 {{.*}}hierarchy "Mid"
// CHECK-DAG: simulation.vpi_lexical_class_typespec.decl @[[TYPE:[^ ]+]] {{.*}}owner @[[MID]] definition @[[BASE]] debug "Base"
// CHECK-DAG: simulation.vpi_class_extends.decl @{{[^ ]+}} {{.*}}owner @[[MID]] {{.*}}base_typespec = @[[TYPE]]
// CHECK-DAG: simulation.vpi_class_parameter_assign.decl @{{[^ ]+}} {{.*}}owner @[[TYPE]] parameter @{{[^ ]+}} ordinal 0
// CHECK-DAG: simulation.vpi_class_parameter_assign.decl @{{[^ ]+}} {{.*}}owner @[[TYPE]] parameter @{{[^ ]+}} ordinal 3
// CHECK-DAG: simulation.vpi_lexical_class_typespec.decl @{{[^ ]+}} {{.*}}unavailable_parameters = "#(.P(7))"
// CHECK-DAG: simulation.vpi_class_extends.decl @{{[^ ]+}} {{.*}}unavailable_base = "T::base"
// CHECK-DAG: simulation.vpi_class_extends.decl @{{[^ ]+}} {{.*}}unavailable_arguments = "(P + 1)"
