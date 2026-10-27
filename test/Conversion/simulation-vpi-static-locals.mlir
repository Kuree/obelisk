// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.StaticLocalScopes
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph{vpi=full},talos-sim-verify-compute-graph,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),convert-talos-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=LLVM
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @static_locals {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.storage.decl 0 in 1 : i32 static hierarchy "top.work.b.dot" debug "b.dot" {vpi_type = #word}
    simulation.storage.decl 1 in 1 : i32 static hierarchy "top.work.block.x" debug "x" {vpi_type = #word}
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.startup"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.work"
    simulation.code_unit.decl 46 in 1 function hierarchy "top.$init" {internal}
    simulation.code_unit.decl 47 in 1 function hierarchy "top.make"
    simulation.vpi_local.decl @a id 1 in 45 scope 1 ordinal 0 hierarchy "top.work.first" debug "first" : i32 {vpi_type = #word, source_ordinal = 0 : i32}
    simulation.vpi_static_local.bind 0 in 45 ordinal 1
    simulation.vpi_local.decl @c id 2 in 45 scope 1 ordinal 1 hierarchy "top.work.last" debug "last" : i32 {vpi_type = #word, source_ordinal = 2 : i32}
    simulation.statement.decl 11 in 45 scope 1 type 33 name "block" {is_scope}
    simulation.statement_site.decl 110 on 11 phase 0
    simulation.vpi_statement_relation.decl code_unit 45 type 20 selector 104 ordinal 0 modes 1 to 11
    simulation.vpi_static_local.bind 1 in 45 within 11 ordinal 0
    simulation.vpi_initializer_binding 46 storage 1
    simulation.vpi_call.decl @init_call id 20 in 46 scope 1 type 19 name "make" callee 47
    simulation.func @startup(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
// LLVM-NOT: simulation.vpi_static_local
// LLVM: llvm.func
// LLVM-NOT: simulation.vpi_static_local
