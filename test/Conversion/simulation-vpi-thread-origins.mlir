// RUN: obelisk-opt %s | FileCheck %s
// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.ThreadOrigins
module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @thread_origins {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 1 hierarchy "top.other" vpi_kind 32
    simulation.code_unit.decl 10 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 20 in 1 always hierarchy "top.unbound"
    simulation.code_unit.decl 60 in 1 function hierarchy "top.fn"
    simulation.code_unit.decl 70 in 2 initial hierarchy "top.other.initial"
    simulation.vpi_object.anchor @module id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @event id 1 type 34 in 1 parent @module ordinal 0
        hierarchy "top.ready" debug "ready"
    simulation.vpi_event_binding @event id 7
    simulation.code_unit.decl 98 in 1 fork hierarchy "top.$unbound"
    simulation.code_unit.decl 99 in 1 fork hierarchy "top.$fork"
    simulation.statement.decl 100 in 10 scope 1 type 33 name "body" {is_scope}
    simulation.statement_site.decl 1000 on 100 phase 0
    simulation.statement.decl 150 in 10 scope 1 type 38 parent 100
    simulation.statement.decl 200 in 10 scope 1 type 38 parent 100
    simulation.statement.decl 300 in 70 scope 2 type 38
    simulation.statement.decl 400 scope 1 type 8
    simulation.vpi_statement_relation.decl code_unit 10 type 24 selector 104 ordinal 0 modes 1 to 100
    simulation.vpi_statement_relation.decl statement 100 type 33 selector 104 ordinal 0 modes 2 to 150
    simulation.vpi_statement_relation.decl statement 100 type 33 selector 104 ordinal 1 modes 2 to 200
    simulation.vpi_statement_relation.decl code_unit 70 type 24 selector 104 ordinal 0 modes 1 to 300
    simulation.vpi_statement_relation.decl scope 1 type 32 selector 8 ordinal 0 modes 2 to 400
    // Source containment is unchanged: the fork points to its original branch.
    simulation.vpi_thread_origin_binding 99 origin 200
    simulation.vpi_thread_origin_binding 10 origin 100
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 10 : i64} {
      %bound = simulation.spawn @branch(%ctx) : !simulation.context -> !simulation.process
      %unbound = simulation.spawn @unbound(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func private @branch(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 99 : i64} {
      %event = simulation.context.event %ctx[7] : !simulation.event
      simulation.suspend.event %event to ^resumed
    ^resumed:
      simulation.return
    }
    simulation.func private @unbound(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 98 : i64} {
      %event = simulation.context.event %ctx[7] : !simulation.event
      simulation.suspend.event %event to ^resumed
    ^resumed:
      simulation.return
    }
  }
}
// CHECK: simulation.statement.decl 200 in 10 scope 1 type 38 parent 100
// CHECK: simulation.vpi_thread_origin_binding 99 origin 200
// CHECK: simulation.vpi_thread_origin_binding 10 origin 100
