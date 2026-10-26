// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.VirtualInterfaceActual

// IEEE 1800-2023 37.29: Actual follows the currently assigned interface or
// exact modport. Escaped source names are independent of display-name
// suffixes; unreferenced modports remain declarations.
!plain = !simulation.virtual_interface<"@bus", "">
!port = !simulation.virtual_interface<"@bus", "m.dot">
!ports = !simulation.unpacked_array<-1 : 1 x !port>
#plain = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "@bus", symbol = @bus_type, modport = "", range = [], children = [], childNames = []>
#port = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "@bus", symbol = @port_type, modport = "m.dot", range = [], children = [], childNames = []>
#ports = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false, range = [-1, 1], children = [#port], childNames = []>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @virtual_interface_actual {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 1 hierarchy "top.first" vpi_kind 601
    simulation.scope.decl 3 parent 1 hierarchy "top.second" vpi_kind 601
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>
    }
    simulation.vpi_object.anchor @first id 1 type 601 in 2 parent @top
        ordinal 0 hierarchy "top.first" debug "first" {
      backing = #simulation.vpi_backing<kind = scope, id = 2 : i64>
    }
    simulation.vpi_object.anchor @second id 2 type 601 in 3 parent @top
        ordinal 1 hierarchy "top.second" debug "second" {
      backing = #simulation.vpi_backing<kind = scope, id = 3 : i64>
    }
    simulation.vpi_object.anchor @first_m id 3 type 606 in 2 parent @first
        ordinal 0 hierarchy "top.first.m" debug "m"
    simulation.vpi_object.anchor @first_dot id 4 type 606 in 2 parent @first
        ordinal 1 hierarchy "top.first.\\m.dot " debug "m.dot"
    simulation.vpi_object.anchor @first_spare id 5 type 606 in 2 parent @first
        ordinal 2 hierarchy "top.first.spare" debug "spare"
    simulation.vpi_object.anchor @second_m id 6 type 606 in 3 parent @second
        ordinal 0 hierarchy "top.second.m" debug "m"
    simulation.vpi_object.anchor @second_dot id 7 type 606 in 3 parent @second
        ordinal 1 hierarchy "top.second.\\m.dot " debug "m.dot"
    simulation.vpi_typespec.decl @bus_type id 0 in 1 owner @top
        hierarchy "@bus" debug "bus" {origin = 1 : i32, target_type = #plain}
    simulation.vpi_typespec.decl @port_type id 1 in 1 owner @top
        hierarchy "@bus" debug "bus.m.dot" {origin = 1 : i32, target_type = #port}
    simulation.class.decl @base id 2 {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @node id 1 extends @base {
      is_abstract = false, is_final = false, is_interface = false}
    simulation.class.field @plain of @node at 0 : !plain {
      is_static = false, is_weak = false}
    simulation.class.field @port of @node at 1 : !port {
      is_static = false, is_weak = false}
    simulation.class.field @ports of @node at 2 : !ports {
      is_static = false, is_weak = false}
    simulation.class.field @shared of @node at 3 : !port {
      is_static = true, is_weak = false}
    simulation.vpi_object.anchor @definition id 8 type 652 in 1 parent @top
        ordinal 2 hierarchy "top.Node" debug "Node" {
      backing = #simulation.vpi_backing<kind = class, symbol = @node>
    }
    simulation.vpi_object.anchor @base_definition id 14 type 652 in 1 parent @top
        ordinal 3 hierarchy "top.Base" debug "Base" {
      backing = #simulation.vpi_backing<kind = class, symbol = @base>
    }
    simulation.vpi_object.anchor @extends id 15 type 677 in 1 parent @definition
        ordinal 0 hierarchy "top.Node.extends" debug "extends"
    simulation.vpi_typespec.decl @base_type id 3 in 1 owner @base_definition
        hierarchy "top.Base$type" debug "Base" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @base, range = [],
          children = [], childNames = []>
    }
    simulation.vpi_typespec.decl @canonical id 2 in 1 owner @definition
        hierarchy "top.Node$type" debug "Node" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @node, range = [],
          children = [], childNames = []>
    }
    simulation.vpi_class_member.decl @plain_member id 9 in 1 owner @canonical
        field @plain hierarchy "top.Node.plain" debug "plain" {vpi_type = #plain}
    simulation.vpi_class_member.decl @port_member id 10 in 1 owner @canonical
        field @port hierarchy "top.Node.port" debug "port" {vpi_type = #port}
    simulation.vpi_class_member.decl @ports_member id 11 in 1 owner @canonical
        field @ports hierarchy "top.Node.ports" debug "ports" {vpi_type = #ports}
    simulation.vpi_class_member.decl @shared_member id 12 in 1 owner @canonical
        field @shared hierarchy "top.Node.shared" debug "shared" {
      vpi_type = #port, static_storage = 4 : i64
    }
    simulation.vpi_class_member.decl @shared_lexical id 13 in 1 owner @definition
        field @shared hierarchy "top.Node.shared" debug "shared" {
      vpi_type = #port, static_storage = 4 : i64
    }
    simulation.vpi_relation.decl <kind = symbol, symbol = @canonical>
        selector 677 handle ordinal 0 to <kind = symbol, symbol = @base_type>
    simulation.vpi_relation.decl <kind = symbol, symbol = @definition>
        selector 677 handle ordinal 0 to <kind = symbol, symbol = @extends>
    simulation.vpi_relation.decl <kind = symbol, symbol = @extends>
        selector 652 handle ordinal 0 to <kind = symbol, symbol = @definition>
    simulation.vpi_relation.decl <kind = symbol, symbol = @extends>
        selector 630 handle ordinal 0 to <kind = symbol, symbol = @base_type>
    simulation.storage.decl 0 in 1 : !plain design hierarchy "top.plain" {vpi_type = #plain}
    simulation.storage.decl 1 in 1 : !port design hierarchy "top.port" {vpi_type = #port}
    simulation.storage.decl 2 in 1 : !ports design hierarchy "top.ports" {vpi_type = #ports}
    simulation.storage.decl 3 in 1 : !simulation.class_handle<@node> design
        hierarchy "top.obj" {
      vpi_type = #simulation.vpi_type<kind = class, isSigned = false,
          isFourState = false, symbol = @node, range = [],
          children = [], childNames = []>
    }
    simulation.storage.decl 4 in 1 : !port design
        hierarchy "top.Node.shared" {vpi_type = #port}
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
