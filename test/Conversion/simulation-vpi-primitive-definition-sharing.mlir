// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test --gtest_filter=GeneratedDesignDatabase.SharedPrimitiveDefinitions

// Built-in definition facts are shared; every occurrence retains its own handle.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @design {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" debug "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_definition.decl @and type 21 name "and" primitive_type 1
    simulation.vpi_definition.decl @tran type 55 name "tran" primitive_type 22
    simulation.vpi_object.anchor @p0 id 1 type 21 in 1 parent @top ordinal 0 hierarchy "top.p0" debug "p0" {primitive_input_count = 0 : i64}
    simulation.vpi_definition.bind @p0 to @and
    simulation.vpi_object.anchor @p1 id 2 type 21 in 1 parent @top ordinal 1 hierarchy "top.p1" debug "p1" {primitive_input_count = 0 : i64}
    simulation.vpi_definition.bind @p1 to @and
    simulation.vpi_object.anchor @p2 id 3 type 21 in 1 parent @top ordinal 2 hierarchy "top.p2" debug "p2" {primitive_input_count = 0 : i64}
    simulation.vpi_definition.bind @p2 to @and
    simulation.vpi_object.anchor @p3 id 4 type 21 in 1 parent @top ordinal 3 hierarchy "top.p3" debug "p3" {primitive_input_count = 0 : i64}
    simulation.vpi_definition.bind @p3 to @and
    simulation.vpi_object.anchor @p4 id 5 type 55 in 1 parent @top ordinal 4 hierarchy "top.p4" debug "p4" {primitive_input_count = 0 : i64}
    simulation.vpi_definition.bind @p4 to @tran
    simulation.vpi_object.anchor @p5 id 6 type 55 in 1 parent @top ordinal 5 hierarchy "top.p5" debug "p5" {primitive_input_count = 0 : i64}
    simulation.vpi_definition.bind @p5 to @tran
    simulation.vpi_object.anchor @p6 id 7 type 55 in 1 parent @top ordinal 6 hierarchy "top.p6" debug "p6" {primitive_input_count = 0 : i64}
    simulation.vpi_definition.bind @p6 to @tran
    simulation.vpi_object.anchor @p7 id 8 type 55 in 1 parent @top ordinal 7 hierarchy "top.p7" debug "p7" {primitive_input_count = 0 : i64}
    simulation.vpi_definition.bind @p7 to @tran
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
