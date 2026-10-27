// RUN: talos-opt %s '--lower-talos-to-sim=vpi=read' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: env TALOS_TEST_INPUT=%t.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.ClassConstraints

// Constraint order follows declaration/prototype position, not name or the
// out-of-block body's position. Each class owns only its own declarations.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 0 : i64} {
    talos.sv.symbol.compilation_unit @cu attributes {hierarchical_name = "$unit", node_id = 1 : i64} {
      talos.sv.type.class_type @base attributes {bitstream_width = 0 : i64,
          declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [],
          has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Base",
          implemented_interfaces = [], is_abstract = true, is_final = false,
          is_interface = false, is_uninstantiated = false, name = "Base",
          node_id = 2 : i64, semantic_type = !talos.class_handle<@root::@cu::@base>} {
        talos.sv.symbol.constraint_block @first attributes {hierarchical_name = "Base::z_first", name = "z_first", node_id = 3 : i64} {}
        talos.sv.symbol.parameter @p attributes {constant_value = "3", hierarchical_name = "Base::P", is_local_param, name = "P", node_id = 4 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
        talos.sv.symbol.constraint_block @external attributes {hierarchical_name = "Base::external", is_extern, is_explicit_extern, name = "external", node_id = 5 : i64, out_of_block_index = 100 : i64} {}
        talos.sv.symbol.constraint_block @shared attributes {hierarchical_name = "Base::a_shared", is_static, name = "a_shared", node_id = 6 : i64} {}
        talos.sv.symbol.constraint_block @required attributes {hierarchical_name = "Base::required", is_extern, is_pure, name = "required", node_id = 7 : i64} {}
      }
      talos.sv.type.class_type @derived attributes {base_class = !talos.class_handle<@root::@cu::@base>, bitstream_width = 0 : i64,
          declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [],
          has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Derived",
          implemented_interfaces = [], is_abstract = false, is_final = false,
          is_interface = false, is_uninstantiated = false, name = "Derived",
          node_id = 8 : i64, semantic_type = !talos.class_handle<@root::@cu::@derived>} {
        talos.sv.symbol.constraint_block @own attributes {hierarchical_name = "Derived::own", name = "own", node_id = 9 : i64} {}
        talos.sv.symbol.constraint_block @required attributes {hierarchical_name = "Derived::required", name = "required", node_id = 10 : i64} {}
      }
      talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "Generic", name = "Generic", node_id = 11 : i64, is_interface = false, specialization_count = 1 : i64} {
        talos.sv.type.class_type @g_int attributes {bitstream_width = 0 : i64,
            declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [],
            has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Generic#(int)",
            implemented_interfaces = [], is_abstract = false, is_final = false,
            is_interface = false, is_uninstantiated = false, name = "Generic", node_id = 12 : i64,
            semantic_type = !talos.class_handle<@root::@cu::@g::@g_int>} {
          talos.sv.symbol.constraint_block @own attributes {hierarchical_name = "Generic#(int)::own", name = "own", node_id = 13 : i64} {}
        }
      }
    }
  }
}

// CHECK-DAG: simulation.vpi_object.anchor @[[BASE:[^ ]+]] {{.*}}type 652 {{.*}}hierarchy "Base"
// CHECK-DAG: simulation.vpi_object.anchor @[[DERIVED:[^ ]+]] {{.*}}type 652 {{.*}}hierarchy "Derived"
// CHECK-DAG: simulation.vpi_typespec.decl @[[BASE_TS:[^ ]+]] {{.*}}owner @[[BASE]] hierarchy "Base"{{.*}}origin = 3 : i32
// CHECK-DAG: simulation.vpi_typespec.decl @[[DERIVED_TS:[^ ]+]] {{.*}}owner @[[DERIVED]] hierarchy "Derived"{{.*}}origin = 3 : i32
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 653 {{.*}}parent @[[BASE]] ordinal 0 hierarchy "Base::z_first"{{.*}}selector = 50 : i32, value = true{{.*}}selector = 604 : i32, value = 0 : i32
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 653 {{.*}}parent @[[BASE]] ordinal 2 hierarchy "Base::external"{{.*}}selector = 604 : i32, value = 2 : i32
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 653 {{.*}}parent @[[BASE]] ordinal 3 hierarchy "Base::a_shared"{{.*}}selector = 604 : i32, value = 0 : i32
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 653 {{.*}}parent @[[BASE]] ordinal 4 hierarchy "Base::required"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 653 {{.*}}parent @[[BASE_TS]] ordinal 0 hierarchy "Base::z_first"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 653 {{.*}}parent @[[BASE_TS]] ordinal 2 hierarchy "Base::external"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 653 {{.*}}parent @[[BASE_TS]] ordinal 3 hierarchy "Base::a_shared"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 653 {{.*}}parent @[[BASE_TS]] ordinal 4 hierarchy "Base::required"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 653 {{.*}}parent @[[DERIVED]] ordinal 0 hierarchy "Derived::own"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 653 {{.*}}parent @[[DERIVED_TS]] ordinal 0 hierarchy "Derived::own"
// CHECK-DAG: simulation.vpi_typespec.decl @[[GENERIC_TS:[^ ]+]] {{.*}}hierarchy "Generic#(int)"{{.*}}origin = 3 : i32
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 653 {{.*}}parent @[[GENERIC_TS]] ordinal 0 hierarchy "Generic#(int)::own"
