// RUN: env TALOS_TEST_INPUT=%s %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.ManagedVirtualInterfaceArrays

// 37.17 and 37.29: array selection retains a live index path through current
// fixed/dynamic/queue dimensions; Actual follows the selected interface value.
!port = !simulation.virtual_interface<"@bus", "m.dot">
!dynamic = !simulation.dynamic_array<!port>
!queue = !simulation.queue<!port, 0>
!fixed = !simulation.unpacked_array<1 : 0 x !port>
!fixed_dynamic = !simulation.unpacked_array<2 : 1 x !dynamic>
!dynamic_fixed = !simulation.dynamic_array<!fixed>
!dynamic_queue = !simulation.dynamic_array<!queue>
#plain = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "@bus", symbol = @bus_type, modport = "", range = [], children = [], childNames = []>
#port = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "@bus", symbol = @port_type, modport = "m.dot", range = [], children = [], childNames = []>
#dynamic = #simulation.vpi_type<kind = dynamic_array, isSigned = false, isFourState = false, range = [], children = [#port], childNames = []>
#queue = #simulation.vpi_type<kind = queue, isSigned = false, isFourState = false, queueBound = 0 : i64, range = [], children = [#port], childNames = []>
#fixed = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [1, 0], children = [#port], childNames = []>
#fixed_dynamic = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [2, 1], children = [#dynamic], childNames = []>
#dynamic_fixed = #simulation.vpi_type<kind = dynamic_array, isSigned = false, isFourState = false, range = [], children = [#fixed], childNames = []>
#dynamic_queue = #simulation.vpi_type<kind = dynamic_array, isSigned = false, isFourState = false, range = [], children = [#queue], childNames = []>
#node = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false, symbol = @node, range = [], children = [], childNames = []>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @managed_interfaces {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 1 hierarchy "top.first" vpi_kind 601
    simulation.scope.decl 3 parent 1 hierarchy "top.second" vpi_kind 601
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_object.anchor @first id 1 type 601 in 2 parent @top ordinal 0
        hierarchy "top.first" debug "first" {
      backing = #simulation.vpi_backing<kind = scope, id = 2 : i64>
    }
    simulation.vpi_object.anchor @second id 2 type 601 in 3 parent @top ordinal 1
        hierarchy "top.second" debug "second" {
      backing = #simulation.vpi_backing<kind = scope, id = 3 : i64>
    }
    simulation.vpi_object.anchor @first_port id 3 type 606 in 2 parent @first
        ordinal 0 hierarchy "top.first.\\m.dot " debug "m.dot"
    simulation.vpi_object.anchor @second_port id 4 type 606 in 3 parent @second
        ordinal 0 hierarchy "top.second.\\m.dot " debug "m.dot"
    simulation.vpi_typespec.decl @bus_type id 2 in 1 owner @top
        hierarchy "@bus" debug "bus" {origin = 1 : i32, target_type = #plain}
    simulation.vpi_typespec.decl @port_type id 0 in 1 owner @top
        hierarchy "@bus" debug "bus.m.dot" {origin = 1 : i32, target_type = #port}
    simulation.class.decl @node id 1 {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.field @ports of @node at 0 : !queue {
      is_static = false, is_weak = false}
    simulation.class.field @shared of @node at 1 : !dynamic {
      is_static = true, is_weak = false}
    simulation.vpi_object.anchor @definition id 5 type 652 in 1 parent @top
        ordinal 2 hierarchy "top.Node" debug "Node" {
      backing = #simulation.vpi_backing<kind = class, symbol = @node>
    }
    simulation.vpi_typespec.decl @canonical id 1 in 1 owner @definition
        hierarchy "top.Node$type" debug "Node" {
      origin = 3 : i32, target_type = #node
    }
    simulation.vpi_class_member.decl @ports_member id 6 in 1 owner @canonical
        field @ports hierarchy "top.Node.ports" debug "ports" {vpi_type = #queue}
    simulation.vpi_class_member.decl @shared_member id 7 in 1 owner @canonical
        field @shared hierarchy "top.Node.shared" debug "shared" {
      vpi_type = #dynamic, static_storage = 6 : i64}
    simulation.vpi_class_member.decl @shared_lexical id 8 in 1 owner @definition
        field @shared hierarchy "top.Node.shared" debug "shared" {
      vpi_type = #dynamic, static_storage = 6 : i64}
    simulation.storage.decl 0 in 1 : !dynamic design hierarchy "top.dynamic" {vpi_type = #dynamic}
    simulation.storage.decl 1 in 1 : !queue design hierarchy "top.queue" {vpi_type = #queue}
    simulation.storage.decl 2 in 1 : !fixed_dynamic design hierarchy "top.fixed_dynamic" {vpi_type = #fixed_dynamic}
    simulation.storage.decl 3 in 1 : !dynamic_fixed design hierarchy "top.dynamic_fixed" {vpi_type = #dynamic_fixed}
    simulation.storage.decl 4 in 1 : !dynamic_queue design hierarchy "top.dynamic_queue" {vpi_type = #dynamic_queue}
    simulation.storage.decl 5 in 1 : !simulation.class_handle<@node> design hierarchy "top.obj" {vpi_type = #node}
    simulation.storage.decl 6 in 1 : !dynamic design hierarchy "top.Node.shared" {vpi_type = #dynamic}
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
