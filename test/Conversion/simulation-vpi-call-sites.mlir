// RUN: obelisk-opt %s | FileCheck %s
// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.CallSites
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @call_sites {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 task hierarchy "top.worker"
    simulation.code_unit.decl 46 in 1 function hierarchy "top.inner"
    simulation.code_unit.decl 47 in 1 fork hierarchy "top.$fork"
    simulation.statement.decl 11 in 44 scope 1 type 60
    simulation.statement.decl 12 in 45 scope 1 type 33 name "block" {is_scope}
    simulation.statement.decl 13 in 45 scope 1 type 3 parent 12
    simulation.statement_site.decl 111 on 11 phase 0
    simulation.statement_site.decl 112 on 12 phase 0
    simulation.statement_site.decl 113 on 13 phase 0
    simulation.statement.decl 14 in 46 scope 1 type 38
    simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
    simulation.vpi_statement_relation.decl code_unit 45 type 59 selector 104 ordinal 0 modes 1 to 12
    simulation.vpi_statement_relation.decl statement 12 type 33 selector 104 ordinal 0 modes 2 to 13
    simulation.vpi_statement_relation.decl code_unit 46 type 20 selector 104 ordinal 0 modes 1 to 14
    simulation.vpi_thread_origin_binding 47 origin 13
    simulation.vpi_call.decl @task_call id 20 in 44 scope 1 type 60 name "worker.with.dot" statement 11 within 11 callee 45
    simulation.vpi_call.decl @nested_call id 21 in 45 scope 1 type 19 name "inner" within 13 callee 46
    simulation.vpi_call.decl @system_call id 22 in 45 scope 1 type 56 name "$system.with.dot" within 13
    simulation.vpi_call.decl @indirect_call id 23 in 45 scope 1 type 648 name "inner" within 13 callee 46 {is_indirect}
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      simulation.task.call @worker(%ctx) arguments 1 to ^done
          {simulation.vpi_call_site = @task_call} : !simulation.context
    ^done:
      simulation.return
    }
    simulation.func private @worker(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 12 : i32, code_unit_id = 45 : i64} {
      simulation.call @inner(%ctx) {simulation.vpi_call_site = @nested_call} : (!simulation.context) -> ()
      simulation.return
    }
    simulation.func private @inner(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 46 : i64} {
      simulation.return
    }
  }
}
// CHECK: simulation.vpi_call.decl @task_call id 20 in 44 scope 1 type 60 name "worker.with.dot" statement 11 within 11 callee 45
// CHECK: simulation.vpi_call.decl @nested_call id 21 in 45 scope 1 type 19 name "inner" within 13 callee 46
// CHECK: simulation.vpi_call_site = @task_call
// CHECK: simulation.vpi_call_site = @nested_call
