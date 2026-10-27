// RUN: env TALOS_TEST_INPUT=%s TALOS_TEST_OUTPUT=%t.dump \
// RUN:   TALOS_TEST_VPI=read %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.Dump
// RUN: FileCheck %s < %t.dump

// IEEE 1800-2023 37.33: a class variable is a VPI object (vpiClassVar) even
// while it is null. Its design storage is the managed reference word, so it
// is named and navigable like any other variable.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @vpi_class_variable {
    simulation.class.decl @item id 1 {debug_name = "item",
      is_abstract = false, is_final = false, is_interface = false}
    simulation.scope.decl 0 hierarchy "\\$root " debug "$root"
    simulation.vpi_definition.decl @top_definition type 32 name "top"
    simulation.scope.decl 1 parent 0 hierarchy "top" debug "top"
      source_definition "top" vpi_kind 32 definition @top_definition
    simulation.vpi_object.anchor @top_anchor id 0 type 32 in 1 ordinal 0
      hierarchy "top" debug "top" {
        backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @item_anchor id 1 type 652 in 1
      parent @top_anchor ordinal 0 hierarchy "top.item" debug "item" {
        backing = #simulation.vpi_backing<kind = class, symbol = @item>}
    simulation.storage.decl 0 in 1 : !simulation.class_handle<@item> design
      hierarchy "top.obj" {
        vpi_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @item, range = [], children = [],
          childNames = []>}

    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}

// CHECK: object name=top.obj kind=2 vpi_kind=615 {{.*}} width=64 {{.*}} type_kind=1 type_flags=0x0
// CHECK: relation {{.*}} source_type=32 mode=iterate selector=100 {{.*}} source_name=top target_name=top.obj
