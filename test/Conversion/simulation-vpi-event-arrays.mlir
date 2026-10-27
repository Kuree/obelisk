// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.EventArrays

// Public declaration identities delegate to exact executable storage. The two
// dimensions use opposite directions, and typedefs name the array and leaves.
#event = #simulation.vpi_type<kind = event, isSigned = false, isFourState = false, range = [], children = [], childNames = []>
#event_alias = #simulation.vpi_type<kind = event, isSigned = false, isFourState = false, range = [], children = [], childNames = [], typedefAliases = [@event_type]>
#row = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [-1, 0], children = [#event_alias], childNames = []>
#grid = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [2, 1], children = [#row], childNames = [], typedefAliases = [@grid_type]>
#other = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [4, 5], children = [#event], childNames = []>
!grid = !simulation.unpacked_array<2 : 1 x !simulation.unpacked_array<-1 : 0 x !simulation.event>>
!other = !simulation.unpacked_array<4 : 5 x !simulation.event>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @event_arrays {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_typespec.decl @event_type id 0 in 1 owner @top
        hierarchy "top.EventAlias" debug "EventAlias" {target_type = #event_alias}
    simulation.vpi_typespec.decl @grid_type id 1 in 1 owner @top
        hierarchy "top.EventGrid" debug "EventGrid" {target_type = #grid}
    simulation.vpi_object.anchor @events id 1 type 129 in 1 parent @top
        ordinal 0 hierarchy "top.events" debug "events" {
      index_ranges = array<i64: 2, 1, -1, 0>}
    simulation.vpi_object.anchor @event0 id 2 type 34 in 1 parent @events
        ordinal 0 hierarchy "top.events[2][-1]" debug "events[2][-1]" {
      member_indices = array<i64: 2, -1>}
    simulation.vpi_object.anchor @event1 id 3 type 34 in 1 parent @events
        ordinal 1 hierarchy "top.events[2][0]" debug "events[2][0]" {
      member_indices = array<i64: 2, 0>}
    simulation.vpi_object.anchor @event2 id 4 type 34 in 1 parent @events
        ordinal 2 hierarchy "top.events[1][-1]" debug "events[1][-1]" {
      member_indices = array<i64: 1, -1>}
    simulation.vpi_object.anchor @event3 id 5 type 34 in 1 parent @events
        ordinal 3 hierarchy "top.events[1][0]" debug "events[1][0]" {
      member_indices = array<i64: 1, 0>}
    simulation.vpi_object.anchor @other id 6 type 129 in 1 parent @top
        ordinal 1 hierarchy "top.other" debug "other" {
      index_ranges = array<i64: 4, 5>}
    simulation.vpi_object.anchor @other0 id 7 type 34 in 1 parent @other
        ordinal 0 hierarchy "top.other[4]" debug "other[4]" {
      member_indices = array<i64: 4>}
    simulation.vpi_object.anchor @other1 id 8 type 34 in 1 parent @other
        ordinal 1 hierarchy "top.other[5]" debug "other[5]" {
      member_indices = array<i64: 5>}
    simulation.storage.decl 0 in 1 : !grid design hierarchy "top.events" {
      simulation.vpi_identity_delegated = @events, vpi_type = #grid}
    simulation.storage.decl 1 in 1 : !other design hierarchy "top.other" {
      simulation.vpi_identity_delegated = @other, vpi_type = #other}
    simulation.storage.decl 2 in 1 : !simulation.event design
        hierarchy "top.scalar" {vpi_type = #event}
    simulation.code_unit.decl 2 in 1 fork hierarchy "top.first_waiter"
    simulation.code_unit.decl 3 in 1 fork hierarchy "top.second_waiter"
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %first = simulation.spawn @first_waiter(%ctx) : !simulation.context -> !simulation.process
      %second = simulation.spawn @second_waiter(%ctx) : !simulation.context -> !simulation.process
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
  }
}
