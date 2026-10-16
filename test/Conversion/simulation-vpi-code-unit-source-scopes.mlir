// RUN: obelisk-opt %s | FileCheck %s
// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.CodeUnitSourceScopes
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @source_scopes {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 0 hierarchy "other" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @f id 1 type 20 in 1 parent @top ordinal 0 hierarchy "top.f.with.dot" debug "f.with.dot"
    simulation.vpi_object.anchor @t id 2 type 59 in 1 parent @top ordinal 1 hierarchy "top.worker" debug "worker"
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.f.with.dot" {dpi.import}
    simulation.code_unit.decl 46 in 1 function hierarchy "top.worker" {dpi.import, dpi.task}
    simulation.code_unit.decl 47 in 1 function hierarchy "top.ordinary"
    simulation.code_unit.decl 48 in 1 function hierarchy "top.unrepresented" {internal}
    simulation.vpi_code_unit_scope.bind 45 to @f
    simulation.vpi_code_unit_scope.bind 46 to @t
    simulation.statement.decl 11 in 44 scope 1 type 60
    simulation.statement_site.decl 111 on 11 phase 0
    simulation.vpi_statement_relation.decl code_unit 44 type 24 selector 104 ordinal 0 modes 1 to 11
    simulation.vpi_call.decl @call_task id 20 in 44 scope 1 type 60 name "worker" statement 11 within 11 callee 46
    simulation.vpi_call.decl @call_func id 21 in 44 scope 1 type 19 name "f.with.dot" within 11 callee 45
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      simulation.return
    }
  }
}
// CHECK: simulation.vpi_code_unit_scope.bind 45 to @f
// CHECK: simulation.vpi_code_unit_scope.bind 46 to @t
