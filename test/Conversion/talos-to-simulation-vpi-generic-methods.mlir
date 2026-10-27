// RUN: talos-opt %s '--lower-talos-to-sim=vpi=read' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: env TALOS_TEST_INPUT=%t.mlir %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.GenericClassMethods

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 0 : i64} {
    talos.sv.symbol.compilation_unit @cu attributes {hierarchical_name = "$unit", node_id = 1 : i64} {
      // These lexical declarations have no realized class specialization,
      // executable method, bound signature, or bound method body.
      talos.sv.symbol.generic_class_def @g attributes {hierarchical_name = "G", name = "G", node_id = 2 : i64, is_interface = false, specialization_count = 0 : i64} {
        talos.sv.source.type_parameter @t attributes {hierarchical_name = "G::T", name = "T", node_id = 3 : i64} {}
        talos.sv.source.class_method @ctor attributes {hierarchical_name = "G::new", name = "new", node_id = 4 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32, is_constructor} {}
        talos.sv.source.class_method @run attributes {hierarchical_name = "G::run", name = "run", node_id = 5 : i64, subroutine_kind = 1 : i32, default_lifetime = 0 : i32, member_visibility = 1 : i32} {}
        talos.sv.source.type_parameter @u attributes {hierarchical_name = "G::U", name = "U", node_id = 6 : i64} {}
        talos.sv.source.class_method @escaped attributes {hierarchical_name = "G::E.p", name = "E.p", node_id = 7 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 2 : i32, is_static} {}
        talos.sv.source.class_method @external attributes {hierarchical_name = "G::external", name = "external", node_id = 8 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32, is_extern} {}
        talos.sv.source.class_method @deferred attributes {hierarchical_name = "G::deferred", name = "deferred", node_id = 9 : i64, subroutine_kind = 0 : i32, default_lifetime = 0 : i32, member_visibility = 0 : i32, is_declared_virtual, is_pure} {}
      }
    }
  }
}

// CHECK-NOT: simulation.class.decl
// CHECK-NOT: simulation.class.method
// CHECK-DAG: simulation.vpi_object.anchor @[[G:[^ ]+]] {{.*}}type 652 {{.*}}hierarchy "G"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 609 {{.*}}parent @[[G]] ordinal 0 hierarchy "G::T"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 20 {{.*}}parent @[[G]] ordinal 1 hierarchy "G::new" debug "new"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 59 {{.*}}parent @[[G]] ordinal 2 hierarchy "G::run" debug "run"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 609 {{.*}}parent @[[G]] ordinal 3 hierarchy "G::U"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 20 {{.*}}parent @[[G]] ordinal 4 hierarchy "G::E.p" debug "E.p"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 20 {{.*}}parent @[[G]] ordinal 5 hierarchy "G::external" debug "external"
// CHECK-DAG: simulation.vpi_object.anchor @{{[^ ]+}} {{.*}}type 20 {{.*}}parent @[[G]] ordinal 6 hierarchy "G::deferred" debug "deferred"
// CHECK-NOT: class_method
// CHECK-NOT: simulation.vpi_typespec.decl
