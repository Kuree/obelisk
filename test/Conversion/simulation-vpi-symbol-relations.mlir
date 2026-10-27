// RUN: talos-opt %s | FileCheck %s --check-prefix=IR
// RUN: env TALOS_TEST_INPUT=%s TALOS_TEST_OUTPUT=%t.dump \
// RUN:   TALOS_TEST_VPI=read %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.Dump
// RUN: FileCheck %s < %t.dump

// IEEE 1800-2023 37.31-32: class definitions reach the base typespec
// through an extends object; a class typespec reaches its base typespec
// directly. Both forms retain their distinct semantic identities.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @symbol_relations {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.class.decl @base_class id 1 {debug_name = "Base",
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @derived_class id 2 {debug_name = "Derived",
      is_abstract = false, is_final = false, is_interface = false}
    simulation.vpi_object.anchor @base id 1 type 652 in 1 parent @top
        ordinal 0 hierarchy "top.Base" debug "Base" {
      backing = #simulation.vpi_backing<kind = class, symbol = @base_class>
    }
    simulation.vpi_object.anchor @derived id 2 type 652 in 1 parent @top
        ordinal 1 hierarchy "top.Derived" debug "Derived" {
      backing = #simulation.vpi_backing<kind = class, symbol = @derived_class>
    }
    simulation.vpi_object.anchor @extends id 3 type 677 in 1 parent @derived
        ordinal 0 hierarchy "top.Derived.extends" debug "extends"
    simulation.vpi_typespec.decl @base_type id 0 in 1 owner @base
        hierarchy "top.Base.base_type" debug "base_type" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @base_class, range = [],
          children = [], childNames = []>
    }
    simulation.vpi_typespec.decl @derived_type id 1 in 1 owner @derived
        hierarchy "top.Derived.derived_type" debug "derived_type" {
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @derived_class, range = [],
          children = [], childNames = []>
    }
    simulation.vpi_typespec.decl @local_type id 2 in 1 owner @base_type
        hierarchy "top.Base.local_type" debug "local_type" {
      target_type = #simulation.vpi_type<kind = reg, isSigned = false,
          isFourState = true, range = [7, 0],
          children = [], childNames = []>
    }
    simulation.code_unit.decl 4 in 1 function
        hierarchy "top.Base.method" debug "method"
    simulation.vpi_object.anchor @method id 4 type 20 in 1 parent @base_type
        ordinal 0 hierarchy "top.Base.method" debug "method" {
      backing = #simulation.vpi_backing<kind = code_unit, id = 4 : i64>
    }
    simulation.storage.decl 0 in 1 : !simulation.logic<1> design
        hierarchy "top.arg0" debug "arg0"
    simulation.storage.decl 1 in 1 : !simulation.logic<1> design
        hierarchy "top.arg1" debug "arg1"
    // Deliberately reverse the declaration order; explicit ordinals determine
    // the observable constructor argument order.
    simulation.vpi_relation.decl <kind = symbol, symbol = @extends>
        selector 89 iterate ordinal 1 to <kind = storage, id = 1 : i64>
    simulation.vpi_relation.decl <kind = symbol, symbol = @extends>
        selector 89 iterate ordinal 0 to <kind = storage, id = 0 : i64>
    simulation.vpi_relation.decl <kind = symbol, symbol = @derived>
        selector 677 handle ordinal 0 to <kind = symbol, symbol = @extends>
    simulation.vpi_relation.decl <kind = symbol, symbol = @extends>
        selector 652 handle ordinal 0 to <kind = symbol, symbol = @derived>
    simulation.vpi_relation.decl <kind = symbol, symbol = @extends>
        selector 630 handle ordinal 0 to <kind = symbol, symbol = @base_type>
    simulation.vpi_relation.decl <kind = symbol, symbol = @base>
        selector 727 iterate ordinal 0 to <kind = symbol, symbol = @derived>
    simulation.vpi_relation.decl <kind = symbol, symbol = @derived_type>
        selector 652 handle ordinal 0 to <kind = symbol, symbol = @derived>
    simulation.vpi_relation.decl <kind = symbol, symbol = @derived_type>
        selector 677 handle ordinal 0 to <kind = symbol, symbol = @base_type>
    simulation.vpi_relation.decl <kind = symbol, symbol = @derived>
        selector 745 iterate ordinal 0 to <kind = symbol, symbol = @top>
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}

// IR: simulation.vpi_relation.decl <kind = symbol, symbol = @derived> selector 677 handle ordinal 0 to <kind = symbol, symbol = @extends>
// IR: simulation.vpi_relation.decl <kind = symbol, symbol = @derived_type> selector 677 handle ordinal 0 to <kind = symbol, symbol = @base_type>
// CHECK-DAG: relation {{.*}} source_type=652 mode=handle selector=677 ordinal=0 {{.*}} source_name=top.Derived target_name=top.Derived.extends
// CHECK-DAG: relation {{.*}} source_type=677 mode=handle selector=652 ordinal=0 {{.*}} source_name=top.Derived.extends target_name=top.Derived
// CHECK-DAG: relation {{.*}} source_type=677 mode=handle selector=630 ordinal=0 {{.*}} source_name=top.Derived.extends target_name=top.Base.base_type
// CHECK-DAG: relation {{.*}} source_type=652 mode=iterate selector=727 ordinal=0 {{.*}} source_name=top.Base target_name=top.Derived
// CHECK-DAG: relation {{.*}} source_type=630 mode=handle selector=652 ordinal=0 {{.*}} source_name=top.Derived.derived_type target_name=top.Derived
// CHECK-DAG: relation {{.*}} source_type=630 mode=handle selector=677 ordinal=0 {{.*}} source_name=top.Derived.derived_type target_name=top.Base.base_type
// CHECK-DAG: relation {{.*}} source_type=630 mode=iterate selector=730 ordinal=0 {{.*}} source_name=top.Base.base_type target_name=top.Base.method
// CHECK-DAG: relation {{.*}} source_type=652 mode=iterate selector=745 ordinal=0 {{.*}} source_name=top.Derived target_name=top
// CHECK-DAG: relation {{.*}} source_type=677 mode=iterate selector=89 ordinal=0 {{.*}} source_name=top.Derived.extends target_name=top.arg0
// CHECK-DAG: relation {{.*}} source_type=677 mode=iterate selector=89 ordinal=1 {{.*}} source_name=top.Derived.extends target_name=top.arg1
// CHECK-DAG: relation {{.*}} source_type=630 mode=iterate selector=725 ordinal=0 {{.*}} source_name=top.Base.base_type target_name=top.Base.local_type
