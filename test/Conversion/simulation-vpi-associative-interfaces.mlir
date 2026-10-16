// RUN: env OBELISK_TEST_INPUT=%s %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.AssociativeVirtualInterfaceArrays

!port = !simulation.virtual_interface<"@bus", "m.dot">
!fixed = !simulation.unpacked_array<2 : 1 x !port>
#plain = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "@bus", symbol = @bus_type, modport = "", range = [], children = [], childNames = []>
#port = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "@bus", symbol = @port_type, modport = "m.dot", range = [], children = [], childNames = []>
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false,
    range = [31, 0], children = [], childNames = []>
#wide_key = #simulation.vpi_type<kind = logic, isSigned = true, isFourState = true,
    range = [129, 0], children = [], childNames = []>
#string = #simulation.vpi_type<kind = string, isSigned = false, isFourState = false,
    range = [], children = [], childNames = []>
#record = #simulation.vpi_type<kind = unpacked_struct, isSigned = false, isFourState = false,
    name = "key_t", range = [], children = [#word, #string], childNames = ["number", "text"],
    isTagged = false, isSoft = false, bitWidth = 0 : i64,
    selectableWidth = 96 : i64, bitstreamWidth = 96 : i64, tagBits = 0 : i64,
    childOrdinals = [0, 1], childPackedOffsets = [0, 0]>
#untyped = #simulation.vpi_type<kind = untyped, isSigned = false, isFourState = false,
    range = [], children = [], childNames = []>
!record = !simulation.unpacked_struct<[
 #simulation.field<name = "number", type = i32, ordinal = 0, packedOffset = 0>,
 #simulation.field<name = "text", type = !simulation.string, ordinal = 1, packedOffset = 0>]>
#fixed = #simulation.vpi_type<kind = unpacked_array, isSigned = false, isFourState = false,
 range = [2, 1], children = [#port], childNames = []>
#node = #simulation.vpi_type<kind = class, isSigned = false, isFourState = false,
 symbol = @node, range = [], children = [], childNames = []>
#process = #simulation.vpi_type<kind = process, isSigned = false, isFourState = false,
 range = [], children = [], childNames = []>
!wide = !simulation.assoc_array<!simulation.logic<130>, !port, true, false>
#wide = #simulation.vpi_type<kind = assoc_array, isSigned = false, isFourState = false,
 range = [], children = [#wide_key, #port], childNames = [], wildcardIndex = false>
!strings = !simulation.assoc_array<!simulation.string, !port, false, false>
#strings = #simulation.vpi_type<kind = assoc_array, isSigned = false, isFourState = false,
 range = [], children = [#string, #port], childNames = [], wildcardIndex = false>
!records = !simulation.assoc_array<!record, !port, false, false>
#records = #simulation.vpi_type<kind = assoc_array, isSigned = false, isFourState = false,
 range = [], children = [#record, #port], childNames = [], wildcardIndex = false>
!wildcard = !simulation.assoc_array<!simulation.box, !port, false, true>
#wildcard = #simulation.vpi_type<kind = assoc_array, isSigned = false, isFourState = false,
 range = [], children = [#untyped, #port], childNames = [], wildcardIndex = true>
!nested = !simulation.assoc_array<i32, !fixed, true, false>
#nested = #simulation.vpi_type<kind = assoc_array, isSigned = false, isFourState = false,
 range = [], children = [#word, #fixed], childNames = [], wildcardIndex = false>
!classes = !simulation.assoc_array<!simulation.class_handle<@node>, !port, false, false>
#classes = #simulation.vpi_type<kind = assoc_array, isSigned = false, isFourState = false,
 range = [], children = [#node, #port], childNames = [], wildcardIndex = false>
!processes = !simulation.assoc_array<!simulation.process, !port, false, false>
#processes = #simulation.vpi_type<kind = assoc_array, isSigned = false, isFourState = false,
 range = [], children = [#process, #port], childNames = [], wildcardIndex = false>
!dynamic = !simulation.dynamic_array<!strings>
#dynamic = #simulation.vpi_type<kind = dynamic_array, isSigned = false, isFourState = false,
 range = [], children = [#strings], childNames = []>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @associative_interfaces {
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
    simulation.vpi_object.anchor @definition id 5 type 652 in 1 parent @top
        ordinal 2 hierarchy "top.Node" debug "Node" {
      backing = #simulation.vpi_backing<kind = class, symbol = @node>}
    simulation.vpi_typespec.decl @canonical id 1 in 1 owner @definition
        hierarchy "top.Node$type" debug "Node" {origin = 3 : i32, target_type = #node}
    simulation.storage.decl 0 in 1 : !wide design hierarchy "top.wide" {vpi_type = #wide}
    simulation.storage.decl 1 in 1 : !strings design hierarchy "top.strings" {vpi_type = #strings}
    simulation.storage.decl 2 in 1 : !records design hierarchy "top.records" {vpi_type = #records}
    simulation.storage.decl 3 in 1 : !wildcard design hierarchy "top.wildcard" {vpi_type = #wildcard}
    simulation.storage.decl 4 in 1 : !nested design hierarchy "top.nested" {vpi_type = #nested}
    simulation.storage.decl 5 in 1 : !classes design hierarchy "top.classes" {vpi_type = #classes}
    simulation.storage.decl 6 in 1 : !processes design hierarchy "top.processes" {vpi_type = #processes}
    simulation.storage.decl 7 in 1 : !dynamic design hierarchy "top.dynamic" {vpi_type = #dynamic}
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
