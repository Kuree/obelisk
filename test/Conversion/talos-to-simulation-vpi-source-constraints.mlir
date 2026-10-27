// RUN: talos-opt %s '--lower-talos-to-sim=vpi=read' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: env TALOS_TEST_INPUT=%t.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SourceClassConstraints

// IEEE 37.31 details 3-4: normal constraints retain prototype/declaration
// order and belong only to their own lexical class, without binding bodies.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 0 : i64} {
    talos.sv.symbol.compilation_unit @cu attributes {hierarchical_name = "$unit", node_id = 1 : i64} {
      talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 2 : i64, is_interface = false, specialization_count = 0 : i64} {
        talos.sv.source.class_constraint @first attributes {hierarchical_name = "G::z_first", name = "z_first", node_id = 3 : i64, has_external_definition = false} {}
        talos.sv.source.type_parameter @t attributes {hierarchical_name = "G::T", name = "T", node_id = 4 : i64} {}
        talos.sv.source.class_constraint @external attributes {hierarchical_name = "G::external", name = "external", node_id = 5 : i64, is_prototype, has_external_definition = true} {}
        talos.sv.source.class_constraint @shared attributes {hierarchical_name = "G::C.dot", name = "C.dot", node_id = 6 : i64, is_static, has_external_definition = false} {}
        talos.sv.source.class_constraint @required attributes {hierarchical_name = "G::required", name = "required", node_id = 7 : i64, is_pure, is_prototype, has_external_definition = false} {}
        talos.sv.source.class_constraint @empty attributes {hierarchical_name = "G::implicit_empty", name = "implicit_empty", node_id = 8 : i64, is_prototype, has_external_definition = false} {}
        talos.sv.source.class_definition @nested attributes {hierarchical_name = "G::Nested", name = "Nested", node_id = 9 : i64, is_abstract = false, is_final = false, is_interface = false, is_parameterized = false} {
          talos.sv.source.class_constraint @own attributes {hierarchical_name = "G::Nested::own", name = "own", node_id = 10 : i64, has_external_definition = false} {}
        }
      }
      talos.sv.symbol.generic_class_def @h attributes {hierarchical_name = "H", name = "H", node_id = 11 : i64, is_interface = false, specialization_count = 1 : i64} {
        talos.sv.source.class_constraint @lexical attributes {hierarchical_name = "H::own", name = "own", node_id = 12 : i64, has_external_definition = false} {}
        talos.sv.type.class_type @h_int attributes {bitstream_width = 0 : i64,
            declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [],
            has_base_constructor_call = false, has_cycles = false, hierarchical_name = "H#(int)",
            implemented_interfaces = [], is_abstract = false, is_final = false,
            is_interface = false, is_uninstantiated = false, name = "H", node_id = 13 : i64,
            semantic_type = !talos.class_handle<@root::@cu::@h::@h_int>} {
          talos.sv.symbol.constraint_block @effective attributes {hierarchical_name = "H#(int)::own", name = "own", node_id = 14 : i64} {}
        }
      }
    }
  }
}
// CHECK-DAG: simulation.vpi_object.anchor @[[G:[^ ]+]] {{.*}}type 652 {{.*}}hierarchy "G"
// CHECK-DAG: simulation.vpi_object.anchor @[[NESTED:[^ ]+]] {{.*}}type 652 {{.*}}parent @[[G]] {{.*}}hierarchy "G::Nested"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 653 {{.*}}parent @[[G]] ordinal 0 hierarchy "G::z_first"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 653 {{.*}}parent @[[G]] ordinal 2 hierarchy "G::external"{{.*}}selector = 604 : i32, value = 2 : i32
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 653 {{.*}}parent @[[G]] ordinal 3 hierarchy "G::C.dot"{{.*}}selector = 2 : i32, value = "C.dot"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 653 {{.*}}parent @[[G]] ordinal 4 hierarchy "G::required"{{.*}}selector = 604 : i32, value = 0 : i32
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 653 {{.*}}parent @[[G]] ordinal 5 hierarchy "G::implicit_empty"{{.*}}selector = 604 : i32, value = 0 : i32
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 653 {{.*}}parent @[[NESTED]] ordinal 0 hierarchy "G::Nested::own"
// CHECK-DAG: simulation.vpi_object.anchor @[[H:[^ ]+]] {{.*}}type 652 {{.*}}hierarchy "H"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 653 {{.*}}parent @[[H]] ordinal 0 hierarchy "H::own"
// CHECK-DAG: simulation.vpi_typespec.decl @[[H_TYPE:[^ ]+]] {{.*}}owner @[[H]] hierarchy "H#(int)"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 653 {{.*}}parent @[[H_TYPE]] ordinal 0 hierarchy "H#(int)::own"
