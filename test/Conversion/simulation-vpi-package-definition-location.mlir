// RUN: env OBELISK_TEST_INPUT=%s OBELISK_TEST_OUTPUT=%t \
// RUN:   OBELISK_TEST_VPI=read %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.Dump
// RUN: FileCheck %s --input-file=%t

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @design {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.vpi_object.anchor @pkg id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg" {
      definition_loc = loc("pkg.sv":7:1),
      vpi_properties = #obelisk_sim.vpi_properties<[
        #obelisk_sim.vpi_property<selector = 9 : i32, value = "pkg">
      ]>
    }
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "$root.initial"
    obelisk_sim.func @initial(%ctx: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk_sim.return
    }
  }
}

// CHECK: static_object name=pkg:: vpi_kind=600 id=0 scope=$root
// CHECK-DAG: fixed_property source_table=3 source=0 selector=9 kind=3 value=pkg
// CHECK-DAG: fixed_property source_table=3 source=0 selector=15 kind=3 value=pkg.sv
// CHECK-DAG: fixed_property source_table=3 source=0 selector=16 kind=1 value=7
