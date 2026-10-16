// RUN: obelisk-opt %s | FileCheck %s --check-prefix=IR
// RUN: env OBELISK_TEST_INPUT=%s OBELISK_TEST_OUTPUT=%t.dump \
// RUN:   OBELISK_TEST_VPI=read %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.Dump:GeneratedDesignDatabase.ClassSpecializationBindings
// RUN: FileCheck %s < %t.dump

// IEEE 1800-2023 37.32: two specializations retain one lexical definition.
// Display names are deliberately equal; symbols carry specialization identity.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @class_specializations {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_object.anchor @generic id 1 type 652 in 1 parent @top
        ordinal 0 hierarchy "top.Generic" debug "Generic"
    simulation.class.decl @one id 1 {debug_name = "Generic",
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @two id 2 {debug_name = "Generic",
      is_abstract = false, is_final = false, is_interface = false}
    simulation.vpi_typespec.decl @two_type id 0 in 1 owner @generic
        hierarchy "top.Generic$two" debug "Generic" {
      origin = 3 : i32,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 74 : i32, value = false>
      ]>,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @two, range = [], children = [],
          childNames = []>
    }
    simulation.vpi_typespec.decl @one_type id 1 in 1 owner @generic
        hierarchy "top.Generic$one" debug "Generic" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @one, range = [], children = [],
          childNames = []>
    }
    simulation.vpi_typespec.decl @alias id 2 in 1 owner @top
        hierarchy "top.Alias" debug "Alias" {
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @one, range = [], children = [],
          childNames = []>
    }
    simulation.storage.decl 0 in 1 : !simulation.class_handle<@one> design
        hierarchy "top.obj" {
      vpi_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @one, range = [], children = [],
          childNames = [], typedefAliases = [@alias]>
    }
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}

// IR: origin = 3 : i32
// CHECK: class_type_binding class_id=1 {{.*}} typespec_name=top.Generic$one
// CHECK: class_type_binding class_id=2 {{.*}} typespec_name=top.Generic$two
// CHECK-DAG: relation {{.*}} source_type=652 mode=iterate selector=630 ordinal=0 {{.*}} source_name=top.Generic target_name=top.Generic$two
// CHECK-DAG: relation {{.*}} source_type=652 mode=iterate selector=630 ordinal=1 {{.*}} source_name=top.Generic target_name=top.Generic$one
// CHECK-DAG: relation {{.*}} source_type=630 mode=handle selector=652 ordinal=0 {{.*}} source_name=top.Generic$one target_name=top.Generic
// CHECK-DAG: relation {{.*}} source_type=630 mode=handle selector=652 ordinal=0 {{.*}} source_name=top.Generic$two target_name=top.Generic
// CHECK-DAG: relation {{.*}} source_type=630 mode=handle selector=84 ordinal=0 {{.*}} source_name=top.Generic$one target_name=top.Generic
