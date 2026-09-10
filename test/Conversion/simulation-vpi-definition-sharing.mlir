// RUN: env OBELISK_TEST_INPUT=%s OBELISK_TEST_OUTPUT=%t \
// RUN:   OBELISK_TEST_VPI=read %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.Dump
// RUN: FileCheck %s --input-file=%t

// Definition-invariant instance metadata is emitted once. Each elaborated
// instance retains a distinct scope cursor and a compact binding to the shared
// source definition; no per-instance fixed-property rows are required.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @design {
    obelisk_sim.vpi_definition.decl @cell type 32 name "cell" definition_loc loc("cell.sv":3:1)
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top.left" debug "left" vpi_kind 32 definition @cell
    obelisk_sim.scope.decl 2 parent 0 hierarchy "top.right" debug "right" vpi_kind 32 definition @cell
    obelisk_sim.code_unit.decl 1 in 1 initial hierarchy "top.left.initial"
    obelisk_sim.func @initial(%ctx: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk_sim.return
    }
  }
}

// CHECK-NOT: fixed_property
// CHECK-COUNT-1: definition index=0 vpi_kind=32 name=cell source=cell.sv:3
// CHECK-DAG: definition_binding source_table=0 source=1 definition=0
// CHECK-DAG: definition_binding source_table=0 source=2 definition=0
