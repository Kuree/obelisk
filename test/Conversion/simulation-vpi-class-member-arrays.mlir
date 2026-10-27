// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.ClassMemberArrays

// IEEE 1800-2023 37.27, 37.32 detail 11 and 37.33 detail 7: named-event
// arrays retain their declared dimensions; virtual-interface iteration
// returns individual declarations, including those whose value is null.
!events = !simulation.unpacked_array<1 : 0 x !simulation.unpacked_array<-1 : 0 x !simulation.event>>
!ports = !simulation.unpacked_array<2 : 1 x !simulation.unpacked_array<-1 : 0 x !simulation.virtual_interface<"@bus", "">>>
#event = #simulation.vpi_type<kind = event, isSigned = false, isFourState = false, range = [], children = [], childNames = []>
#events = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [1, 0], children = [#simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [-1, 0], children = [#event], childNames = []>], childNames = []>
#port = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "@bus", symbol = @bus_type, modport = "", range = [], children = [], childNames = []>
#ports = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [2, 1], children = [#simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [-1, 0], children = [#port], childNames = []>], childNames = []>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @class_member_arrays {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.class.decl @node id 1 {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.field @ready of @node at 0 : !simulation.event {
      is_static = false, is_weak = false}
    simulation.class.field @events of @node at 1 : !events {
      is_static = false, is_weak = false}
    simulation.class.field @shared_events of @node at 2 : !events {
      is_static = true, is_weak = false}
    simulation.class.field @single_port of @node at 3 : !simulation.virtual_interface<"@bus", ""> {
      is_static = false, is_weak = false}
    simulation.class.field @ports of @node at 4 : !ports {
      is_static = false, is_weak = false}
    simulation.vpi_object.anchor @definition id 1 type 652 in 1 parent @top
        ordinal 0 hierarchy "top.Node" debug "Node" {
      backing = #simulation.vpi_backing<kind = class, symbol = @node>
    }
    simulation.vpi_typespec.decl @canonical id 0 in 1 owner @definition
        hierarchy "top.Node$type" debug "Node" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @node, range = [],
          children = [], childNames = []>
    }
    simulation.vpi_typespec.decl @bus_type id 1 in 1 owner @top
        hierarchy "@bus" debug "bus" {origin = 1 : i32, target_type = #port}
    simulation.vpi_class_member.decl @ready_member id 2 in 1 owner @canonical
        field @ready hierarchy "top.Node.ready" debug "ready" {vpi_type = #event}
    simulation.vpi_class_member.decl @events_member id 3 in 1 owner @canonical
        field @events hierarchy "top.Node.events" debug "events" {vpi_type = #events}
    simulation.vpi_class_member.decl @shared_member id 4 in 1 owner @canonical
        field @shared_events hierarchy "top.Node.shared_events" debug "shared_events" {
      vpi_type = #events, static_storage = 1 : i64
    }
    simulation.vpi_class_member.decl @single_port_member id 5 in 1 owner @canonical
        field @single_port hierarchy "top.Node.single_port" debug "single_port" {vpi_type = #port}
    simulation.vpi_class_member.decl @ports_member id 6 in 1 owner @canonical
        field @ports hierarchy "top.Node.ports" debug "ports" {vpi_type = #ports}
    simulation.vpi_class_member.decl @shared_lexical id 7 in 1 owner @definition
        field @shared_events hierarchy "top.Node.shared_events" debug "shared_events" {
      vpi_type = #events, static_storage = 1 : i64
    }
    simulation.storage.decl 0 in 1 : !simulation.class_handle<@node> design
        hierarchy "top.obj" {
      vpi_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @node, range = [],
          children = [], childNames = []>
    }
    simulation.storage.decl 1 in 1 : !events design
        hierarchy "top.Node.shared_events" {vpi_type = #events}
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
