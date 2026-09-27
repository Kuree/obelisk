// RUN: env OBELISK_TEST_INPUT=%s OBELISK_TEST_OUTPUT=%t.dump \
// RUN:   OBELISK_TEST_VPI=read %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.Dump
// RUN: FileCheck %s < %t.dump

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @dpi_import_anchor {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" {vpi_kind = 32 : i32}

    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_object.anchor @dpi_import id 1 type 20 in 1 parent @top
        ordinal 0 hierarchy "top.dpi_import" debug "dpi_import" {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 50 : i32, value = true>
      ]>
    }

    // A source-level DPI import and its executable ABI thunk share a name.
    // The anchor is the VPI function; the thunk remains internal and unindexed.
    simulation.code_unit.decl 1 in 1 function hierarchy "top.dpi_import" {
      simulation.dpi_import
    }
    simulation.code_unit.decl 2 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      simulation.return
    }
  }
}

// CHECK: object name=top.dpi_import kind=7 vpi_kind=0 caps=0x20
// CHECK: static_object name=top.dpi_import vpi_kind=20 id=4 scope=top
// CHECK: fixed_property {{.*}} selector=50 kind=0 value=true
// CHECK: relation {{.*}} source_type=32 mode=iterate selector={{[0-9]+}} ordinal=0 {{.*}} source_name=top target_name=top.dpi_import
