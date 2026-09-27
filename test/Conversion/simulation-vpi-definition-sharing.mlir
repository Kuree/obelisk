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
  simulation.design @design {
    simulation.vpi_definition.decl @cell type 32 name "cell" definition_loc loc("cell.sv":3:1)
    simulation.vpi_definition_member.decl @cell_a of @cell type 28 ordinal 0
        name "a" direction input loc("cell.sv":3:13)
    simulation.vpi_definition_member.decl @cell_z of @cell type 28 ordinal 1
        name "z" direction output loc("cell.sv":3:32)
    simulation.vpi_definition_specialization.decl @cell_spec of @cell
    simulation.vpi_definition_member.specialize @cell_spec member @cell_a type
        #simulation.vpi_type<kind = logic, isSigned = false,
          isFourState = true, range = [7, 0], children = [], childNames = []>
    simulation.vpi_definition_member.specialize @cell_spec member @cell_z type
        #simulation.vpi_type<kind = bit, isSigned = false,
          isFourState = false, range = [0, 0], children = [], childNames = []>
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top.left" debug "left"
        vpi_kind 32 definition @cell specialization @cell_spec
    simulation.scope.decl 2 parent 0 hierarchy "top.right" debug "right"
        vpi_kind 32 definition @cell specialization @cell_spec
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.left.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}

// CHECK-NOT: fixed_property
// CHECK-COUNT-1: definition index=0 vpi_kind=32 name=cell source=cell.sv:3
// CHECK-COUNT-1: definition_member index=0 definition=0 definition_name=cell name=a vpi_kind=28 flags=0x1 direction=1 source=cell.sv:3:13
// CHECK-COUNT-1: definition_member index=1 definition=0 definition_name=cell name=z vpi_kind=28 flags=0x2 direction=2 source=cell.sv:3:32
// CHECK-COUNT-1: definition_member_relation index=0 definition=0 definition_name=cell selector=28 flags=0x0 targets=[0:2)
// CHECK-COUNT-1: definition_specialization index=0 definition=0 definition_name=cell bindings=[0:2)
// CHECK-COUNT-1: definition_specialization_binding index=0 specialization=0 definition=0 member=0 member_name=a semantic_type=
// CHECK-COUNT-1: definition_specialization_binding index=1 specialization=0 definition=0 member=1 member_name=z semantic_type=
// CHECK-DAG: definition_binding source_table=0 source=1 definition=0 specialization=0 source_name=top.left
// CHECK-DAG: definition_binding source_table=0 source=2 definition=0 specialization=0 source_name=top.right
