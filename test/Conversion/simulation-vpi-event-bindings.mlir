// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.EventBindings

// Inventory IDs are deliberately different from executable event identities.
// In particular, event zero is a valid named event rather than a null event.
!events = !simulation.unpacked_array<0 : 1 x !simulation.event>
#event = #simulation.vpi_type<kind = event, isSigned = false, isFourState = false, range = [], children = [], childNames = []>
#events = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [0, 1], children = [#event], childNames = []>
#node = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false, symbol = @node, range = [], children = [], childNames = []>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @events {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_object.anchor @first id 6 type 34 in 1 parent @top
        ordinal 0 hierarchy "top.first" debug "first"
    simulation.vpi_object.anchor @second id 8 type 34 in 1 parent @top
        ordinal 1 hierarchy "top.second" debug "second"
    simulation.vpi_object.anchor @unbound id 7 type 34 in 1 parent @top
        ordinal 2 hierarchy "top.unbound" debug "unbound"
    simulation.vpi_object.anchor @alias id 9 type 34 in 1 parent @top
        ordinal 4 hierarchy "top.alias" debug "alias" {
      backing = #simulation.vpi_backing<kind = storage, id = 1 : i64>}
    simulation.vpi_event_binding @first id 0
    simulation.vpi_event_binding @second id 7
    simulation.class.decl @node id 1 {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.field @ready of @node at 0 : !simulation.event {
      is_static = false, is_weak = false}
    simulation.class.field @events of @node at 1 : !events {
      is_static = false, is_weak = false}
    simulation.class.field @shared of @node at 2 : !simulation.event {
      is_static = true, is_weak = false}
    simulation.vpi_object.anchor @definition id 1 type 652 in 1 parent @top
        ordinal 3 hierarchy "top.Node" debug "Node" {
      backing = #simulation.vpi_backing<kind = class, symbol = @node>}
    simulation.vpi_typespec.decl @canonical id 0 in 1 owner @definition
        hierarchy "top.Node$type" debug "Node" {
      origin = 3 : i32, target_type = #node}
    simulation.vpi_class_member.decl @ready_member id 2 in 1 owner @canonical
        field @ready hierarchy "top.Node.ready" debug "ready" {vpi_type = #event}
    simulation.vpi_class_member.decl @events_member id 3 in 1 owner @canonical
        field @events hierarchy "top.Node.events" debug "events" {vpi_type = #events}
    simulation.vpi_class_member.decl @shared_member id 4 in 1 owner @canonical
        field @shared hierarchy "top.Node.shared" debug "shared" {
      vpi_type = #event, static_event = 7 : i64}
    simulation.vpi_class_member.decl @shared_lexical id 5 in 1 owner @definition
        field @shared hierarchy "top.Node.shared" debug "shared" {
      vpi_type = #event, static_event = 7 : i64}
    simulation.storage.decl 0 in 1 : !simulation.class_handle<@node> design
        hierarchy "top.obj" {vpi_type = #node}
    simulation.storage.decl 1 in 1 : !simulation.event design
        hierarchy "top.alias" {vpi_type = #event}
    simulation.code_unit.decl 2 in 1 fork hierarchy "top.first_waiter"
    simulation.code_unit.decl 3 in 1 fork hierarchy "top.second_waiter"
    simulation.code_unit.decl 4 in 1 fork hierarchy "top.ordered_waiter"
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %first = simulation.spawn @first_waiter(%ctx) : !simulation.context -> !simulation.process
      %second = simulation.spawn @second_waiter(%ctx) : !simulation.context -> !simulation.process
      %ordered = simulation.spawn @ordered_waiter(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func private @first_waiter(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 2 : i64} {
      %event = simulation.context.event %ctx[0] : !simulation.event
      simulation.suspend.event %event to ^resumed
    ^resumed:
      simulation.return
    }
    simulation.func private @second_waiter(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 3 : i64} {
      %event = simulation.context.event %ctx[7] : !simulation.event
      simulation.suspend.event %event to ^resumed
    ^resumed:
      simulation.return
    }
    simulation.func private @ordered_waiter(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 4 : i64} {
      %first = simulation.context.event %ctx[0] : !simulation.event
      %second = simulation.context.event %ctx[7] : !simulation.event
      simulation.suspend.event_order %first, %second events 2 to ^resumed : !simulation.event, !simulation.event
    ^resumed:
      simulation.return
    }
  }
}
